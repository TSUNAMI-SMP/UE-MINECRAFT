package dev.tsunami.bridge.mixin;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.ai.goal.GoalSelector;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
/** Read-only vanilla AI diagnostic access. */
@Mixin(MobEntity.class)
public interface DiagnosticMobAccessor {
    @Accessor("goalSelector") GoalSelector bridgeDiagnosticGoals();
    @Accessor("targetSelector") GoalSelector bridgeDiagnosticTargets();
}
