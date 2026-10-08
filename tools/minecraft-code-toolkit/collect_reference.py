#!/usr/bin/env python3
"""Collect local Loom sources and vanilla reference assets; Python standard library only."""
import argparse
import fnmatch
import hashlib
import json
import pathlib
import re
import sys
import zipfile
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent


def local_path(path):
    """Support long Minecraft package paths without changing Windows settings."""
    path = path.resolve()
    if sys.platform == 'win32':
        value = str(path)
        if not value.startswith('\\\\?\\'):
            value = '\\\\?\\UNC\\' + value[2:] if value.startswith('\\\\') else '\\\\?\\' + value
        path = pathlib.Path(value)
    return path


def digest(data, algorithm='sha256'):
    return hashlib.new(algorithm, data).hexdigest()


def safe_member(name):
    p = pathlib.PurePosixPath(name)
    if p.is_absolute() or '\\' in name or ':' in name or '..' in p.parts:
        raise ValueError('Unsafe archive path: ' + name)
    return name


def write(root, name, data):
    p = root / safe_member(name)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)


def dump(path, obj):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def resource_path(kind, identifier, suffix):
    namespace, _, value = identifier.partition(':')
    if not value:
        namespace, value = 'minecraft', namespace
    return safe_member(f'assets/{namespace}/{kind}/{value}{suffix}')


def source_inputs(inputs):
    jars = sorted(inputs.glob('minecraft-*-sources.jar'))
    if len(jars) != 2 or not any('common-' in j.name for j in jars) or not any('clientonly-' in j.name for j in jars):
        raise ValueError('Both common and clientonly source JARs are required. Run stageReferenceInputs.')
    sources, origins, provenance = {}, {}, []
    for jar in jars:
        if '1.21.11+build.6-v2' not in jar.name:
            raise ValueError('Unexpected Minecraft/mapping revision: ' + jar.name)
        provenance.append({'file': jar.name, 'sha256': digest(jar.read_bytes())})
        with zipfile.ZipFile(jar) as archive:
            if archive.testzip():
                raise ValueError('Corrupt source archive: ' + jar.name)
            for name in archive.namelist():
                if not name.endswith('.java'):
                    continue
                safe_member(name)
                data = archive.read(name)
                if name in sources and sources[name] != data:
                    raise ValueError('Conflicting source entry: ' + name)
                sources[name], origins[name] = data, jar.name
    if not sources:
        raise ValueError('Source JARs contain no Java files')
    return sources, origins, provenance


def dependencies(sources, seeds):
    """Follow explicit imports, wildcard imports and referenced sibling classes."""
    names = {p[:-5].replace('/', '.'): p for p in sources}
    packages = {}
    for p in sources:
        packages.setdefault(p.rsplit('/', 1)[0], {})[pathlib.PurePosixPath(p).stem] = p
    selected, pending = set(), list(seeds)
    while pending:
        p = pending.pop()
        if p in selected:
            continue
        selected.add(p)
        text = sources[p].decode('utf-8')
        refs = set()
        for static, qualified in re.findall(r'^\s*import\s+(static\s+)?([\w.*]+)\s*;', text, re.M):
            if qualified.endswith('.*') and not static:
                refs.update(packages.get(qualified[:-2].replace('.', '/'), {}).values())
            else:
                candidate = qualified
                while '.' in candidate:
                    if candidate in names:
                        refs.add(names[candidate])
                        break
                    candidate = candidate.rsplit('.', 1)[0]
        siblings = packages.get(p.rsplit('/', 1)[0], {})
        for token in set(re.findall(r'\b[A-Z]\w*\b', text)):
            if token in siblings:
                refs.add(siblings[token])
        pending.extend(refs - selected)
    return selected


def vanilla_assets(client):
    result, missing, queue = {}, set(), []
    with zipfile.ZipFile(client) as archive:
        all_names = set(archive.namelist())

        def take(name, required=False):
            if name in result:
                return
            if name not in all_names:
                if required:
                    missing.add(name)
                return
            result[safe_member(name)] = archive.read(name)
            if name.endswith('.png') and name + '.mcmeta' in all_names:
                result[name + '.mcmeta'] = archive.read(name + '.mcmeta')
            if name.endswith('.json') and '/models/' in name:
                queue.append(name)

        for name in ['assets/minecraft/blockstates/grass_block.json',
                     'assets/minecraft/models/block/grass_block.json',
                     'assets/minecraft/models/block/grass_block_snow.json',
                     'assets/minecraft/items/grass_block.json',
                     'assets/minecraft/textures/colormap/grass.png',
                     'assets/minecraft/textures/colormap/foliage.png']:
            take(name, True)
        for name in sorted(all_names):
            if (name.startswith(('assets/minecraft/textures/gui/', 'assets/minecraft/textures/environment/',
                                 'assets/minecraft/shaders/')) or name in ('assets/minecraft/lang/ja_jp.json', 'assets/minecraft/lang/en_us.json')):
                if not name.endswith('/'):
                    take(name)
        # Follow models referred to by both the blockstate and the item definition.
        def models(value):
            if isinstance(value, dict):
                for key, item in value.items():
                    if key == 'model' and isinstance(item, str):
                        take(resource_path('models', item, '.json'), True)
                    else:
                        models(item)
            elif isinstance(value, list):
                for item in value:
                    models(item)
        for name in ('assets/minecraft/blockstates/grass_block.json', 'assets/minecraft/items/grass_block.json'):
            if name in result:
                models(json.loads(result[name]))
        while queue:
            model = json.loads(result[queue.pop()])
            parent = model.get('parent', '')
            if parent and not parent.startswith('builtin/'):
                take(resource_path('models', parent, '.json'), True)
            for texture in model.get('textures', {}).values():
                if not texture.startswith('#'):
                    take(resource_path('textures', texture, '.png'), True)
        if missing:
            raise ValueError('Required vanilla assets missing: ' + ', '.join(sorted(missing)))
    return result


def collect(inputs, output, targets):
    sources, origins, provenance = source_inputs(inputs)
    info = json.loads((inputs / 'mojang_minecraft_info.json').read_text(encoding='utf-8'))
    if info.get('id') != targets['minecraft']:
        raise ValueError('Official version metadata does not match 1.21.11')
    client = inputs / 'minecraft-client.jar'
    data = client.read_bytes()
    expected = info['downloads']['client']['sha1']
    if digest(data, 'sha1') != expected:
        raise ValueError('Vanilla client SHA-1 differs from the official Mojang version metadata')
    assets = vanilla_assets(client)
    mapping = (inputs / 'mappings.tiny').read_bytes()
    if not mapping.startswith(b'tiny\t2\t0') or b'\tnamed' not in mapping.splitlines()[0]:
        raise ValueError('Expected Tiny v2 named mappings')
    output.mkdir(parents=True, exist_ok=False)
    groups, seeds, unmatched = {}, set(), []
    for topic in targets['topics']:
        group = set()
        for pattern in topic['patterns']:
            found = {p for p in sources if fnmatch.fnmatchcase(p, pattern)}
            group.update(found)
            if not found:
                unmatched.append({'topic': topic['id'], 'pattern': pattern})
        if not group:
            raise ValueError('No sources matched topic: ' + topic['id'])
        groups[topic['id']] = group
        seeds.update(group)
    selected = dependencies(sources, seeds)
    index = []
    for name, content in sorted(sources.items()):
        write(output, 'sources/all/' + name, content)
        index.append({'path': name, 'sha256': digest(content), 'jar': origins[name],
                      'selected': name in selected,
                      'topics': [key for key, paths in groups.items() if name in paths]})
    for name in sorted(selected):
        write(output, 'sources/selected/' + name, sources[name])
    for name, content in sorted(assets.items()):
        write(output, 'vanilla/' + name, content)
    dump(output / 'source-index.json', index)
    dump(output / 'asset-index.json', [{'path': n, 'sha256': digest(d)} for n, d in sorted(assets.items())])
    write(output, 'mappings.tiny', mapping)
    dump(output / 'version.json', info)
    dump(output / 'external-assets.json', {'asset_index': info.get('assetIndex'),
         'note': 'Sound definitions and OGG files are external asset objects, not files in the vanilla client JAR. This code collector does not download them.'})
    dump(output / 'source-targets.json', targets)
    dump(output / 'unmatched-patterns.json', unmatched)
    provenance.append({'file': client.name, 'sha256': digest(data), 'official_sha1': expected,
                       'url': info['downloads']['client']['url']})
    provenance.append({'file': 'mappings.tiny', 'sha256': digest(mapping)})
    report = {'minecraft': targets['minecraft'], 'mappings': targets['mappings'],
              'loom': '1.14.10', 'decompiler': 'Loom bundled Vineflower',
              'created_utc': datetime.now(timezone.utc).isoformat(), 'inputs': provenance,
              'all_source_count': len(sources), 'selected_source_count': len(selected),
              'direct_topic_source_count': len(seeds), 'asset_count': len(assets),
              'topic_counts': {k: len(v) for k, v in groups.items()},
              'unmatched_pattern_count': len(unmatched),
              'limitations': ['Decompiled Java with Yarn names, not Mojang original source files.',
                              'Vanilla resources only; active resource packs and runtime biome/world values are not captured.',
                              'Third-party library source is not downloaded; full Minecraft sources allow additional dependency investigation.',
                              'External sounds.json and OGG objects are not collected; their official asset index is recorded in external-assets.json.',
                              'No UE conversion or gameplay fixes are performed.']}
    dump(output / 'report.json', report)
    hits, guide = [], ['# Minecraft 1.21.11 コード調査資料', '',
        'Yarn名の逆コンパイル結果です。Mojangの開発用原本ソースではありません。',
        'この資料はUE用への変換・修正を行いません。sources/all に全Minecraftクラス、',
        'sources/selected に調査対象と追跡可能なimport・同一パッケージ依存を置きます。',
        '同名メソッドの呼び出し元も全ソース内で検索してください。', '',
        'vanilla は追加リソースパックを適用しない基準画像・モデルです。',
        '実プレイのバイオーム色・当たり判定・サーバー状態は別途計測が必要です。',
        '音イベント・音量・ピッチのコードは含みますが、外部 sounds.json/OGG は取得しません。', '',
        '## 草ブロック', '',
        'vanilla/assets/minecraft/models/block/grass_block.json の側面には、',
        '土を含む #side と tintindex=0 の #overlay が同じ面に定義されています。',
        '#top と #overlay を草色で着色し、#side/#bottom の土は着色しません。',
        '下地・オーバーレイの両方、透過処理、面の重複排除、設置時の着色更新を確認してください。',
        '今回の黒い土・白い草の実際の原因をこの資料だけで断定するものではありません。', '']
    for topic in targets['topics']:
        guide += ['## ' + topic['title'], '']
        for name in sorted(groups[topic['id']]):
            # Keep the guide navigable; the JSON indices enumerate every file.
            if '*' not in ''.join(topic['patterns']) or any(name == p for p in topic['patterns']):
                guide.append('- [ ' + name + ' ](sources/selected/' + name + ')')
            for line_no, line in enumerate(sources[name].decode('utf-8').splitlines(), 1):
                for term in topic['terms']:
                    if term.lower() in line.lower():
                        hits.append({'topic': topic['id'], 'path': name, 'line': line_no,
                                     'term': term, 'text': line.strip()})
        guide += ['', '検索語: ' + ', '.join(topic['terms']), '']
    dump(output / 'entrypoints.json', hits)
    (output / 'README.md').write_text('\n'.join(guide) + '\n', encoding='utf-8')
    with zipfile.ZipFile(output / 'Minecraft-Reference-selected.zip', 'x', zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(output.rglob('*')):
            if p.is_file() and p.suffix != '.zip' and 'sources/all/' not in p.relative_to(output).as_posix():
                archive.write(p, p.relative_to(output).as_posix())
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=pathlib.Path, default=ROOT / 'build/reference-inputs')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        targets = json.loads((ROOT / 'source-targets.json').read_text(encoding='utf-8'))
        report = collect(local_path(args.inputs), local_path(args.output), targets)
        print(json.dumps({k: report[k] for k in ('all_source_count', 'selected_source_count', 'asset_count', 'topic_counts')}, ensure_ascii=True))
        print('Result: ' + str(args.output.resolve()))
    except (ValueError, OSError, KeyError, zipfile.BadZipFile) as exc:
        print('Collection failed: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
