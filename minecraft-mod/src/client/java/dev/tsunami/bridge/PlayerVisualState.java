package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import dev.tsunami.bridge.mixin.HeldItemRendererAccessor;
import dev.tsunami.bridge.mixin.LivingEntityVisualAccessor;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.option.Perspective;
import net.minecraft.entity.player.PlayerModelPart;
import net.minecraft.entity.player.PlayerSkinType;
import net.minecraft.item.ItemStack;
import net.minecraft.item.consume.UseAction;
import net.minecraft.registry.Registries;
import net.minecraft.util.Arm;
import net.minecraft.util.Hand;
import net.minecraft.util.math.MathHelper;

/** Samples native appearance; UE retains authority over movement and interaction outcomes. */
public final class PlayerVisualState {
    private static ItemStack controllerUseItem = ItemStack.EMPTY;
    private static int controllerUseTicks;
    private PlayerVisualState() { }

    public static void tick(MinecraftClient client, boolean controllerMode) {
        if (client.player == null) { reset(); return; }
        if (controllerMode) ((LivingEntityVisualAccessor) client.player).bridgeTickHandSwing();
        boolean held = controllerMode && client.currentScreen == null && !client.isPaused()
            && client.options.useKey.isPressed()
            && client.player.getMainHandStack().getUseAction() != UseAction.NONE;
        ItemStack selected = client.player.getMainHandStack();
        if (!held) { controllerUseItem = ItemStack.EMPTY; controllerUseTicks = 0; }
        else {
            if (!ItemStack.areItemsAndComponentsEqual(controllerUseItem, selected)) controllerUseTicks = 0;
            controllerUseItem = selected.copy();
            controllerUseTicks = Math.min(controllerUseTicks + 1, 72_000);
        }
    }

    public static void reset() { controllerUseItem = ItemStack.EMPTY; controllerUseTicks = 0; }

    /** This overload does not send ClientPlayerEntity's vanilla swing packet to a server. */
    public static void swing(MinecraftClient client) {
        if (client.player != null) client.player.swingHand(Hand.MAIN_HAND, false);
    }

    public static JsonObject sample(MinecraftClient client, boolean controllerMode) {
        JsonObject json = new JsonObject();
        if (client.player == null) return json;
        float tick = MathHelper.clamp(client.getRenderTickCounter().getTickProgress(false), 0, 1);
        var nativeEquip = (HeldItemRendererAccessor) client.gameRenderer.firstPersonRenderer;
        boolean using = controllerMode ? controllerUseTicks > 0 : client.player.isUsingItem();
        ItemStack used = controllerMode ? controllerUseItem : client.player.getActiveItem();
        int usedTicks = controllerMode ? controllerUseTicks : client.player.getItemUseTime();
        UseAction action = using ? used.getUseAction() : UseAction.NONE;
        int layers = 0;
        for (PlayerModelPart part : PlayerModelPart.values())
            if (client.options.isPlayerModelPartEnabled(part)) layers |= part.getBitFlag();
        Perspective perspective = client.options.getPerspective();
        json.addProperty("perspective", perspective == Perspective.FIRST_PERSON ? 0
            : perspective == Perspective.THIRD_PERSON_BACK ? 1 : 2);
        json.addProperty("skinLayers", layers);
        json.addProperty("slimArms", client.player.getSkin().model() == PlayerSkinType.SLIM);
        json.addProperty("leftHanded", client.options.getMainArm().getValue() == Arm.LEFT);
        json.addProperty("swingProgress", MathHelper.clamp(client.player.getHandSwingProgress(tick), 0, 1));
        json.addProperty("equipProgress", MathHelper.lerp(tick, nativeEquip.bridgeLastEquipMain(), nativeEquip.bridgeEquipMain()));
        json.addProperty("equipOff", MathHelper.lerp(tick, nativeEquip.bridgeLastEquipOff(), nativeEquip.bridgeEquipOff()));
        json.addProperty("usingItem", using);
        json.addProperty("useAction", action.asString());
        // Vanilla bows reach their charged pose at 20 ticks; eating/drinking uses item duration.
        int duration = action == UseAction.BOW ? 20 : Math.max(1, used.getMaxUseTime(client.player));
        json.addProperty("useProgress", MathHelper.clamp((usedTicks + tick) / duration, 0, 1));
        json.addProperty("useTicks", usedTicks);
        json.addProperty("offItem", client.player.getOffHandStack().isEmpty() ? ""
            : Registries.ITEM.getId(client.player.getOffHandStack().getItem()).toString());
        return json;
    }
}
