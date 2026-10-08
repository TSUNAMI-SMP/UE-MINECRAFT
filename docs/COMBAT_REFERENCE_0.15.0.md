# Native combat reference (Minecraft Java 1.21.11)

The supplied, locally decompiled Yarn-named reference was used to check the
following behaviors. Original Minecraft source files and sound samples are not
included in this repository.

| Reference location | Converted behavior |
| --- | --- |
| `PlayerEntity.attack`, `getAttackCooldownProgress`, `getAttackCooldownDamageModifier` | Attack-speed period of `20 / speed` ticks; integer tick counter; attacks sample half a tick and the HUD samples zero; base damage scales by `0.2 + 0.8 * charge²`. A full-charge condition is strictly greater than 0.9. |
| `PlayerEntity.isCriticalHit`, `canUseSweepAttack`, `knockbackTarget`, `doSweepingAttack` | Falling critical damage multiplier 1.5, charged sprint bonus knockback 0.5, attacker horizontal velocity multiplied by 0.6, ground sword sweep conditions and nearby-target bounds/distance. |
| `PlayerEntity.playAttackSound`, `addAttackParticlesAndSounds` | No-damage, weak, strong, critical, knockback and sweep events selected from the attack result. The attack event plays at the player's position at volume and pitch 1. |
| `MinecraftClient.doAttack`, `doItemUse`, `handleBlockBreaking` | Air misses reset attack charge and swing without a fabricated air-swing sound. Block mining does not reset the melee charge. Right-click swings only for a successful action that requests a swing. |
| `LivingEntity.damage` | Twenty-tick regeneration timer; its first ten ticks reject equal/weaker damage. Stronger damage applies only the excess and does not refresh red feedback, hurt sound, or base knockback. |
| `LivingEntity.takeKnockback` | Resistance scales strength. Horizontal velocity halves before the impulse. A grounded target's vertical velocity is capped at 0.4 blocks/tick; an airborne target retains its vertical velocity. Fatal full hits also receive this impulse. |
| `DamageUtil.getDamageLeft`, `getInflictedDamage` | Armor/toughness and protection reduction helpers; stronger-hit comparison uses the incoming amount before armor reduction. |
| `LivingEntity.getSoundPitch`, `BatEntity`, `WolfEntity`, `SlimeEntity` | Original triangular pitch variation, child pitch offset and species sound volume. Stronger hits inside the hurt window do not replay hurt sounds. |
| `LivingEntity.updatePostDeath`, `LivingEntityRenderer.setupTransforms` | Twenty-tick death lifetime and the original square-root rotation curve. Death retains normal terrain/velocity motion instead of snapping the rendered bounds onto a floor. |
| `BowItem.onStoppedUsing`, `getPullProgress` | Tick-based bow pull and original randomized launch-sound pitch. A bow without ammunition does not begin use in survival. |

Portable tests cover tick boundaries, excess-damage acceptance, grounded/airborne
knockback, resistance, sequential sprint impulses, armor, death rotation, sweep
conditions and sound pitch. These tests exercise the converted production math;
they do not execute Unreal's collision, rendering, audio or animation systems.

This is not a complete Minecraft combat runtime. Native inventory stacks do not
yet carry all enchantment/status-effect/equipment components; blindness, ladders,
mounts, weapon-specific piercing/mace rules and team exemptions require the
corresponding world/entity systems. The converted sweep currently implements
the unenchanted sword baseline (one damage to secondary targets). Native player
movement has no potion-modified movement-speed attribute. Exact visual/physics
comparison still requires a Windows UE and Minecraft session with matching
initial states.
