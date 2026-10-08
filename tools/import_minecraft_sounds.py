"""Import local decoded Minecraft/resource-pack WAVs for UE-only play.

The exporter resolves sounds.json events into weighted variants. The importer
validates every sample before editing UE, and retains previously assigned audio
on an incomplete import. Copyrighted game sound assets never enter this repo.
"""
import hashlib
import json
import math
import pathlib
import re
import wave


def load_sound_manifest(filename):
    path = pathlib.Path(filename).expanduser().resolve()
    if not path.is_file() or path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError("Missing/oversized sound manifest")
    raw = path.read_bytes()
    manifest = json.loads(raw)
    if not isinstance(manifest, dict) or manifest.get("kind") != "sounds" or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise ValueError("Unsupported native sound manifest")
    sounds = manifest.get("sounds")
    if not isinstance(sounds, dict) or not 1 <= len(sounds) <= 4096:
        raise ValueError("Invalid sound event count")
    files, total_bytes, total_variants = {}, 0, 0
    for event_id, variants in sounds.items():
        if not isinstance(event_id, str) or not re.fullmatch(r"[a-z0-9_.-]+:[a-z0-9_./-]+", event_id):
            raise ValueError("Invalid sound event registry ID")
        if not isinstance(variants, list) or not 1 <= len(variants) <= 256:
            raise ValueError("Invalid sound variant count for " + event_id)
        for variant in variants:
            if not isinstance(variant, dict):
                raise ValueError("Invalid sound variant")
            relative, digest = variant.get("file"), variant.get("sha256")
            if not isinstance(relative, str) or not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
                raise ValueError("Invalid sound path/checksum")
            pure = pathlib.PurePosixPath(relative)
            if pure.is_absolute() or ".." in pure.parts or "\\" in relative or ":" in relative or pure.suffix.lower() != ".wav":
                raise ValueError("Sound path escapes export or is not WAV")
            source = (path.parent / pure).resolve()
            if not source.is_relative_to(path.parent) or not source.is_file() or source.stat().st_size > 10 * 1024 * 1024:
                raise ValueError("Missing/oversized/escaping sound sample")
            for key, low, high in (("volume", 0, 16), ("pitch", .1, 4)):
                value = variant.get(key, 1)
                if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
                    raise ValueError("Invalid sound " + key)
                variant[key] = value
            weight = variant.get("weight", 1)
            if type(weight) is not int or not 1 <= weight <= 1024:
                raise ValueError("Invalid sound variant weight")
            variant["weight"] = weight
            if source in files and files[source] != digest:
                raise ValueError("Conflicting sound file checksums")
            if source not in files:
                if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
                    raise ValueError("Sound sample checksum mismatch")
                try:
                    with wave.open(str(source), "rb") as audio:
                        if audio.getcomptype() != "NONE" or audio.getsampwidth() != 2 or audio.getnchannels() not in (1, 2) or not 8000 <= audio.getframerate() <= 96000 or not 1 <= audio.getnframes() <= audio.getframerate() * 120:
                            raise ValueError("Sound sample must be 16-bit mono/stereo PCM WAV, <=120 seconds")
                        expected = audio.getnframes() * audio.getnchannels() * 2
                        if len(audio.readframes(audio.getnframes())) != expected:
                            raise ValueError("Truncated WAV sample")
                except (wave.Error, EOFError) as error:
                    raise ValueError("Invalid WAV sample: " + str(error)) from error
                total_bytes += source.stat().st_size
                files[source] = digest
            total_variants += 1
            if total_bytes > 512 * 1024 * 1024 or total_variants > 20000:
                raise ValueError("Native sound export budget exceeded")
            variant["source"] = str(source)
    manifest["manifestHash"] = hashlib.sha256(raw).hexdigest()
    return manifest


def import_minecraft_sounds(filename):
    manifest = load_sound_manifest(filename)
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / "UEBridge.uproject").is_file():
        raise RuntimeError("Run native sound imports in the built UEBridge project")
    palette_class = getattr(unreal, "BridgeNativeSoundPalette", None)
    if palette_class is None or not hasattr(unreal, "BridgeNativeSoundEvent") or not hasattr(unreal, "BridgeNativeSoundVariant"):
        raise RuntimeError("Build the updated UEBridge before importing native sounds")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None or unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Stop Play and save the current level before importing sounds")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or world.get_path_name().startswith("/Temp/"):
        raise RuntimeError("Save the current level to your project before importing sounds")
    receivers = [actor for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(actor, unreal.BridgeReceiver)]
    if len(receivers) != 1:
        raise RuntimeError("The saved level must contain exactly one BridgeReceiver")
    receiver = receivers[0]
    previous = receiver.get_editor_property("native_sound_palette")
    assets, tools = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools()
    root, waves, events = "/Game/Bridge/Minecraft/Sounds", {}, []
    unreal.log("Minecraft sound import: validated events=" + str(len(manifest["sounds"])))
    for event_id, variants in sorted(manifest["sounds"].items()):
        entries = []
        for source in variants:
            digest = source["sha256"]
            audio = waves.get(digest)
            if audio is None:
                name = "S_Minecraft_" + digest[:24]
                audio = unreal.load_asset(root + "/" + name)
                if audio is None:
                    task = unreal.AssetImportTask()
                    for key, value in dict(filename=source["source"], destination_path=root, destination_name=name,
                                           automated=True, replace_existing=False, save=True).items():
                        task.set_editor_property(key, value)
                    tools.import_asset_tasks([task])
                    audio = unreal.load_asset(root + "/" + name)
                if not isinstance(audio, unreal.SoundWave) or not assets.save_loaded_asset(audio, False):
                    raise RuntimeError("Cannot import/save sound sample for " + event_id)
                waves[digest] = audio
            variant = unreal.BridgeNativeSoundVariant()
            for key, value in dict(wave=audio, volume=source["volume"], pitch=source["pitch"], weight=source["weight"]).items():
                variant.set_editor_property(key, value)
            entries.append(variant)
        event = unreal.BridgeNativeSoundEvent()
        event.set_editor_property("id", event_id)
        event.set_editor_property("variants", entries)
        events.append(event)
    name = "DA_MinecraftSounds_v1_" + manifest["manifestHash"][:24]
    palette = unreal.load_asset(root + "/" + name)
    previous_events = list(palette.get_editor_property("events")) if isinstance(palette, palette_class) else None
    if palette is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", palette_class)
        palette = tools.create_asset(name, root, palette_class, factory)
    if not isinstance(palette, palette_class):
        raise RuntimeError("Cannot create native sound palette")
    def restore_events():
        if previous_events is not None:
            try:
                palette.set_editor_property("events", previous_events)
                if not assets.save_loaded_asset(palette, False):
                    unreal.log_error("Cannot save restored native sound palette")
            except Exception as restore_error:
                unreal.log_error("Cannot restore native sound palette: " + str(restore_error))
    try:
        palette.set_editor_property("events", events)
        if not assets.save_loaded_asset(palette, False):
            raise RuntimeError("Cannot save native sound palette")
    except Exception:
        restore_events()
        raise
    try:
        with unreal.ScopedEditorTransaction("Assign Minecraft native sounds"):
            receiver.set_editor_property("native_sound_palette", palette)
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level() or receiver.get_editor_property("native_sound_palette") != palette:
            raise RuntimeError("Cannot save/verify native sound palette assignment")
    except Exception:
        restore_events()
        try:
            receiver.set_editor_property("native_sound_palette", previous)
            if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
                unreal.log_error("Cannot save restored native sound palette assignment")
        except Exception as restore_error:
            unreal.log_error("Cannot restore native sound palette assignment: " + str(restore_error))
        raise
    unreal.log("Minecraft native sounds ready: events=" + str(len(events)) + " samples=" + str(len(waves)) + "; assigned and saved")
    return palette
