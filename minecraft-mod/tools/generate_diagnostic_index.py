"""Usage: python tools/generate_diagnostic_index.py /path/to/Yarn-1.21.11-build.6/mappings.tiny
Run from minecraft-mod; consumes mapping metadata only, never Minecraft class files.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1])
lines=p.read_text().splitlines(); groups=[]; current=[]
for l in lines[1:]:
 if l.startswith('c\t'):
  if current: groups.append(current)
  current=[l]
 elif current: current.append(l)
if current: groups.append(current)
prefix=('net/minecraft/entity/ai/goal/','net/minecraft/entity/ai/control/','net/minecraft/entity/ai/pathing/','net/minecraft/entity/ai/brain/','net/minecraft/client/render/item/model/')
exact=('entity/Entity','entity/LivingEntity','entity/mob/MobEntity','entity/mob/PathAwareEntity','entity/passive/PassiveEntity','entity/passive/AnimalEntity','entity/passive/PigEntity','entity/passive/CowEntity','entity/passive/SheepEntity','entity/passive/ChickenEntity','entity/passive/VillagerEntity','entity/passive/FoxEntity','entity/mob/ZombieEntity','entity/mob/AbstractSkeletonEntity','entity/mob/SkeletonEntity','entity/mob/CreeperEntity','entity/player/PlayerEntity','client/network/ClientPlayerEntity','client/Mouse','client/render/Camera','client/render/GameRenderer','client/render/WorldRenderer','client/render/item/HeldItemRenderer','client/render/block/BlockModelRenderer','client/render/block/BlockRenderManager','client/render/model/BlockModelPart','client/render/item/ItemRenderer','client/render/item/ItemRenderState','client/render/RenderLayer','client/render/RenderLayers','client/font/TextRenderer','client/gui/DrawContext','client/render/DiffuseLighting','client/option/GameOptions')
exact=tuple('net/minecraft/'+s for s in exact)
selected=[g for g in groups if g[0].split('\t')[-1].startswith(prefix) or any(g[0].split('\t')[-1]==e or g[0].split('\t')[-1].startswith(e+'$') for e in exact)]
root=Path(__file__).resolve().parents[1]/'src/client/resources/diagnostics'
(root/'classes.tsv').write_text('# Yarn 1.21.11+build.6; intermediary\tnamed\n'+''.join('\t'.join(g[0].split('\t')[2:4])+'\n' for g in selected))
# Full class name table resolves types referenced by selected member descriptors. Only selected classes' member names are included.
out=[lines[0]]
for g in groups: out.extend(g if g in selected else [g[0]])
(root/'mappings.tiny').write_text('\n'.join(out)+'\n')
print('Selected classes:',len(selected),'mapping bytes:',(root/'mappings.tiny').stat().st_size)
assert len(selected)<=768
