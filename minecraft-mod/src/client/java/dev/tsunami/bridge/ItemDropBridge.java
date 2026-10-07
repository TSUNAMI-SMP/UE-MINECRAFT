package dev.tsunami.bridge;

import com.google.gson.JsonObject;
import java.io.IOException;
import java.util.*;
import java.util.function.*;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents;
import net.minecraft.client.MinecraftClient;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.Registries;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.storage.ReadView;
import net.minecraft.storage.WriteView;
import net.minecraft.util.math.Vec3d;

/** Dedicated integrated-world item escrow. Inventory mutation happens only on the
 * server thread. Its escrow is saved in the same player NBT as the inventory, so
 * restarting restores outstanding transient UE drops without MC item entities. */
public final class ItemDropBridge {
    private static final String SAVE_KEY="UEBridgeItemEscrow";
    private static final Map<ServerPlayerEntity,ServerState> STATES=Collections.synchronizedMap(new WeakHashMap<>());
    private static Supplier<BridgeTransport> transport=()->null;
    private static BooleanSupplier active=()->false;
    private static Consumer<String> message=text->{};
    private static final LinkedHashMap<String,Pending> pending=new LinkedHashMap<>();
    private static final Set<String> processing=new HashSet<>();
    private static IntegratedServer sourceServer;
    private static UUID sourcePlayer;
    private static String receiver="",session="",epoch=UUID.randomUUID().toString();
    private static volatile int generation;
    private static long lastConnected;
    private static final class Pending {
        JsonObject payload;long lastSend;boolean spawned,closed;
    }
    private static final class ServerState {
        ServerPlayerEntity player;
        ItemDropLedger<ItemStack> ledger;
        final List<ItemStack> recovery=new ArrayList<>();
        ServerState(ServerPlayerEntity owner) {player=owner;ledger=new ItemDropLedger<>((token,count)->insert(player,token,count));}
        void recover() {
            ledger.retryRefunds();
            for(var iterator=recovery.iterator();iterator.hasNext();) {
                ItemStack stack=iterator.next();stack.decrement(insert(player,stack,stack.getCount()));if(stack.isEmpty()) iterator.remove();
            }
        }
        void release() {
            ledger.refundAll();
            for(var balance:ledger.balances()) recovery.add(balance.token().copyWithCount(balance.count()));
            ledger=new ItemDropLedger<>((token,count)->insert(player,token,count));recover();
        }
        List<ItemStack> save() {
            List<ItemStack> stacks=new ArrayList<>();for(var balance:ledger.balances()) stacks.add(balance.token().copyWithCount(balance.count()));
            for(ItemStack stack:recovery) if(!stack.isEmpty()) stacks.add(stack.copy());return stacks;
        }
    }
    static {
        ServerPlayerEvents.AFTER_RESPAWN.register((previous,current,alive)->{
            ServerState transferred=STATES.remove(previous);
            if(transferred!=null) {ServerState recovered=STATES.remove(current);transferred.player=current;
                if(recovered!=null) transferred.recovery.addAll(recovered.recovery);STATES.put(current,transferred);}
        });
        ServerTickEvents.END_SERVER_TICK.register(server->{
            synchronized(STATES) {for(ServerState state:STATES.values()) if(state.player.getEntityWorld().getServer()==server) state.recover();}
        });
        ServerLifecycleEvents.SERVER_STOPPING.register(server->{
            synchronized(STATES) {for(ServerState state:STATES.values()) if(state.player.getEntityWorld().getServer()==server) {state.release();}}
        });
        ServerLifecycleEvents.SERVER_STOPPED.register(server->{
            synchronized(STATES) {STATES.entrySet().removeIf(entry->entry.getKey().getEntityWorld().getServer()==server);}
        });
    }
    private ItemDropBridge() {}
    public static String epoch() {return epoch;}
    public static void configure(Supplier<BridgeTransport> connection,BooleanSupplier controlRequested,Consumer<String> feedback) {
        transport=connection;active=controlRequested;message=feedback;
    }
    private static ServerState state(ServerPlayerEntity player) {return STATES.computeIfAbsent(player,ServerState::new);}
    public static void writeEscrow(ServerPlayerEntity player,WriteView view) {
        ServerState state=STATES.get(player);if(state==null) return;
        List<ItemStack> balances=state.save();
        if(balances.isEmpty()) view.remove(SAVE_KEY);else view.put(SAVE_KEY,ItemStack.CODEC.listOf(),balances);
    }
    public static void readEscrow(ServerPlayerEntity player,ReadView view) {
        // This key belongs only to this mod; absent keys leave all existing player data untouched.
        var balances=view.read(SAVE_KEY,ItemStack.CODEC.listOf());if(balances.isEmpty()) return;
        ServerState state=state(player);state.recovery.clear();
        for(ItemStack stack:balances.get()) if(!stack.isEmpty()) state.recovery.add(stack.copy());
    }
    /** Conservative insertion for creative and survival alike. Vanilla insertStack
     * discards overflow in creative, which would incorrectly acknowledge a pickup. */
    private static int insert(ServerPlayerEntity player,ItemStack token,int requested) {
        if(requested<=0 || token.isEmpty()) return 0;
        var inventory=player.getInventory();int remaining=requested;
        for(int slot=0;slot<inventory.size() && remaining>0;slot++) {
            if(slot>=36 && slot!=net.minecraft.entity.player.PlayerInventory.OFF_HAND_SLOT) continue;
            ItemStack current=inventory.getStack(slot);
            if(!current.isEmpty() && ItemStack.areItemsAndComponentsEqual(token,current)) {
                int amount=Math.min(remaining,Math.max(0,current.getMaxCount()-current.getCount()));current.increment(amount);remaining-=amount;
            }
        }
        for(int slot=0;slot<36 && remaining>0;slot++) if(inventory.getStack(slot).isEmpty()) {
            int amount=Math.min(remaining,token.getMaxCount());inventory.setStack(slot,token.copyWithCount(amount));remaining-=amount;
        }
        if(remaining<requested) {inventory.markDirty();player.currentScreenHandler.sendContentUpdates();}
        return requested-remaining;
    }
    public static void request(MinecraftClient client,boolean entireStack) {
        BridgeTransport bridge=transport.get();
        if(client.player==null || client.world==null || !active.getAsBoolean() || bridge==null || !bridge.itemDropsSupported()) {
            message.accept("UEアイテム投下の準備ができていません。MODとUEを同じ版へ更新してください");return;
        }
        IntegratedServer server=client.getServer();var pose=bridge.authorityPose();
        if(server==null || server.isRemote() || pose==null || !bridge.diagnostics().ueControl()) {message.accept("専用シングルプレイのUE操作中に投下してください");return;}
        ItemStack expected=client.player.getMainHandStack().copy();if(expected.isEmpty()) return;
        if(pending.values().stream().filter(entry->!entry.closed).count()>=128) {message.accept("投下アイテムが128件に達しています。拾うかUE操作を終了してください");return;}
        int slot=client.player.getInventory().getSelectedSlot(),amount=entireStack ? expected.getCount() : 1;
        String model;
        try {model=ItemModelExport.modelKey(client,expected);} catch(RuntimeException error) {message.accept("アイテム識別に失敗しました: "+error.getMessage());return;}
        String id=UUID.randomUUID().toString();Pending entry=new Pending();pending.put(id,entry);
        sourceServer=server;sourcePlayer=client.player.getUuid();receiver=bridge.diagnostics().receiverId();session=bridge.session();
        String importId=bridge.diagnostics().importId(),itemEpoch=epoch;int epoch=generation;UUID owner=sourcePlayer;
        Vec3d forward=client.player.getRotationVec(1).normalize();
        double eye=client.player.isSneaking() ? 1.27 : 1.62;
        Vec3d position=new Vec3d(pose.x(),pose.y()+eye-.30,pose.z()).add(forward.multiply(.35));
        Vec3d velocity=forward.multiply(6).add(0,2,0);
        server.execute(()->{
            ServerPlayerEntity player=server.getPlayerManager().getPlayer(owner);
            if(epoch!=generation || player==null || server.isRemote()) {client.execute(()->pending.remove(id));return;}
            ItemStack current=player.getInventory().getStack(slot);
            if(!ItemStack.areItemsAndComponentsEqual(current,expected) || current.getCount()<amount || player.getInventory().getSelectedSlot()!=slot) {
                client.execute(()->{pending.remove(id);message.accept("投下前にインベントリが変わりました。もう一度投下してください");});return;
            }
            var ledger=state(player).ledger;ItemStack token=current.copyWithCount(1);
            if(!ledger.reserve(id,token,amount,()->{current.decrement(amount);player.getInventory().markDirty();player.currentScreenHandler.sendContentUpdates();})) {
                client.execute(()->{pending.remove(id);message.accept("投下取引の上限に達しました。UE操作を終了してから再開してください");});return;
            }
            JsonObject payload=new JsonObject();payload.addProperty("itemTx",id);payload.addProperty("importId",importId);payload.addProperty("itemEpoch",itemEpoch);
            payload.addProperty("itemId",Registries.ITEM.getId(token.getItem()).toString());payload.addProperty("itemModelKey",model);
            payload.addProperty("itemCount",amount);payload.addProperty("itemMaxCount",token.getMaxCount());payload.add("position",vector(position));payload.add("velocity",vector(velocity));
            client.execute(()->{if(epoch==generation && pending.containsKey(id)) entry.payload=payload;});
        });
    }
    private static JsonObject vector(Vec3d v) {JsonObject result=new JsonObject();result.addProperty("x",v.x);result.addProperty("y",v.y);result.addProperty("z",v.z);return result;}
    public static void tick(MinecraftClient client) {
        BridgeTransport bridge=transport.get();long now=System.nanoTime();
        if(bridge!=null && bridge.diagnostics().connected()) lastConnected=now;
        if(sourceServer!=null && (!active.getAsBoolean() || bridge==null || !session.equals(bridge.session())
                || !receiver.equals(bridge.diagnostics().receiverId()) || now-lastConnected>2_000_000_000L)) {stop(client);return;}
        if(bridge==null) return;
        JsonObject feedback;int maximum=32;
        while(maximum-->0 && (feedback=bridge.pollItemFeedback())!=null) accept(client,bridge,feedback);
        if(!bridge.diagnostics().connected()) return;
        for(Pending entry:pending.values()) if(!entry.spawned && entry.payload!=null && now-entry.lastSend>=100_000_000L) {
            try {JsonObject packet=bridge.packet("item_drop");entry.payload.entrySet().forEach(field->packet.add(field.getKey(),field.getValue()));bridge.auxiliary(packet);entry.lastSend=now;}
            catch(IOException error) {break;}
        }
    }
    private static void accept(MinecraftClient client,BridgeTransport bridge,JsonObject feedback) {
        if(sourceServer==null || sourcePlayer==null) return;
        String id=feedback.get("itemTx").getAsString();if(!pending.containsKey(id)) return;
        int revision=feedback.get("itemRevision").getAsInt(),count=feedback.get("itemCount").getAsInt();String action=feedback.get("itemAction").getAsString();
        String receiptKey=id+":"+revision;if(!processing.add(receiptKey)) return;
        int epoch=generation;IntegratedServer server=sourceServer;UUID owner=sourcePlayer;
        server.execute(()->{
            ServerPlayerEntity player=server.getPlayerManager().getPlayer(owner);
            ItemDropLedger.Receipt receipt=epoch==generation && player!=null ? state(player).ledger.result(id,revision,action,count) : null;
            client.execute(()->{
                processing.remove(receiptKey);if(epoch!=generation || receipt==null) return;
                Pending entry=pending.get(id);if(entry==null) return;
                boolean reportRejection="rejected".equals(action) && !entry.closed;
                if("spawned".equals(action)) entry.spawned=true;
                if("rejected".equals(action)) {entry.spawned=true;entry.closed=true;}
                try {JsonObject packet=bridge.packet("item_resolve");packet.addProperty("itemTx",id);packet.addProperty("itemRevision",revision);packet.addProperty("itemAccepted",receipt.accepted());bridge.auxiliary(packet);}
                catch(IOException ignored) { /* The actor retries this exact revision; the server returns its cached receipt. */ }
                if(reportRejection) message.accept((receipt.closed() ? "UE投下を戻しました: " : "UE投下失敗。インベントリに空きを作ると残数を返します: ")+feedback.get("itemReason").getAsString());
                // Retain closed transaction tombstones so a lost final resolve is ACKed again.
                if(receipt.closed()) {entry.spawned=true;entry.closed=true;}
                while(pending.size()>512) {String retired=pending.entrySet().stream().filter(item->item.getValue().closed).map(Map.Entry::getKey).findFirst().orElse(null);if(retired==null) break;pending.remove(retired);}
            });
        });
    }
    /** Only full control/session exit calls this. Chat/inventory pauses keep escrow. */
    public static void stop(MinecraftClient client) {
        generation++;epoch=UUID.randomUUID().toString();IntegratedServer server=sourceServer;UUID owner=sourcePlayer;
        sourceServer=null;sourcePlayer=null;receiver=session="";pending.clear();processing.clear();lastConnected=0;
        if(server!=null && owner!=null) server.execute(()->{ServerPlayerEntity player=server.getPlayerManager().getPlayer(owner);if(player!=null) {ServerState state=state(player);state.release();}});
    }
    public static String status() {long live=pending.values().stream().filter(entry->!entry.closed).count();return "投下取引="+live+" / 完了記録="+(pending.size()-live)+"（終了時は残数を返却、満杯分はプレイヤーデータへ保管）";}
}
