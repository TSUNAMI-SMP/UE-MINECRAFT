package dev.tsunami.bridge;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.util.Locale;
import java.util.UUID;

/** Fully validated UE effect feedback, without loading Minecraft classes on the transport path. */
public record VanillaFeedbackData(String session, String receiverId, String effectId, long sequence,
                                  Type type, String block, Point position, Point listener,
                                  double listenerYaw, double listenerPitch, double fallDistance) {
    public enum Type { BREAK, PLACE, STEP, LAND }
    public record Point(double x, double y, double z) {
        Point subtract(Point other) { return new Point(x-other.x, y-other.y, z-other.z); }
        double dot(Point other) { return x*other.x + y*other.y + z*other.z; }
    }

    /** Null means malformed; callers must not ACK invalid packets. All fields have strict JSON types. */
    public static VanillaFeedbackData parse(JsonObject packet) {
        try {
            if (packet == null || !number(packet, "v") || packet.get("v").getAsDouble()!=1
                    || !"feedback".equals(string(packet, "kind"))) return null;
            String session=uuid(string(packet,"session")), receiver=uuid(string(packet,"receiverId"));
            String effect=uuid(string(packet,"effectId")), block=string(packet,"block");
            if (session==null || receiver==null || effect==null || block==null || block.length()>256
                    || !block.matches("[a-z0-9_.-]+:[a-z0-9_./-]+")) return null;
            double sequence=finite(packet,"seq",1,9_007_199_254_740_991.0);
            if (sequence!=Math.rint(sequence)) return null;
            String name=string(packet,"type");
            Type type=switch (name==null ? "" : name) {
                case "break" -> Type.BREAK; case "place" -> Type.PLACE;
                case "step" -> Type.STEP; case "land" -> Type.LAND; default -> null;
            };
            if (type==null) return null;
            Point source=new Point(coordinate(packet,"x"),coordinate(packet,"y"),coordinate(packet,"z"));
            Point listener=new Point(coordinate(packet,"listenerX"),coordinate(packet,"listenerY"),coordinate(packet,"listenerZ"));
            double yaw=finite(packet,"listenerYaw",-10_000_000,10_000_000);
            double pitch=finite(packet,"listenerPitch",-90,90);
            double fall=packet.has("fallDistance") ? finite(packet,"fallDistance",0,1000) : 0;
            return new VanillaFeedbackData(session,receiver,effect,(long)sequence,type,block,source,listener,yaw,pitch,fall);
        } catch (IllegalArgumentException | IllegalStateException ex) { return null; }
    }
    private static String string(JsonObject packet,String field) {
        JsonElement value=packet.get(field);
        return value!=null && value.isJsonPrimitive() && value.getAsJsonPrimitive().isString() ? value.getAsString() : null;
    }
    private static boolean number(JsonObject packet,String field) {
        JsonElement value=packet.get(field);
        return value!=null && value.isJsonPrimitive() && value.getAsJsonPrimitive().isNumber();
    }
    private static String uuid(String value) {
        if (value==null || value.length()!=36) return null;
        String canonical=UUID.fromString(value).toString();
        // Unreal's FGuid commonly uses uppercase hex. Keep the wire value for matching receiverId.
        return canonical.equals(value.toLowerCase(Locale.ROOT)) ? value : null;
    }
    private static double finite(JsonObject packet,String field,double minimum,double maximum) {
        if (!number(packet,field)) throw new IllegalArgumentException("Feedback field must be a number");
        double value=packet.get(field).getAsDouble();
        if (!Double.isFinite(value) || value<minimum || value>maximum) throw new IllegalArgumentException("Feedback field out of range");
        return value;
    }
    private static double coordinate(JsonObject packet,String field) { return finite(packet,field,-100_000,100_000); }

    /** Sound source expressed as right, up, forward offsets from the UE camera, in Minecraft blocks. */
    public Point listenerSpace() {
        double yaw=Math.toRadians(listenerYaw), pitch=Math.toRadians(listenerPitch);
        double sinYaw=Math.sin(yaw), cosYaw=Math.cos(yaw), sinPitch=Math.sin(pitch), cosPitch=Math.cos(pitch);
        Point relative=position.subtract(listener);
        Point right=new Point(-cosYaw,0,-sinYaw);
        Point up=new Point(-sinYaw*sinPitch,cosPitch,cosYaw*sinPitch);
        Point forward=new Point(-sinYaw*cosPitch,-sinPitch,cosYaw*cosPitch);
        return new Point(relative.dot(right),relative.dot(up),relative.dot(forward));
    }
    /** The formulas used by vanilla BlockItem, WorldEventHandler, and Entity step/fall sounds. */
    public float volume(float groupVolume) {
        return switch(type) {
            case BREAK, PLACE -> (groupVolume+1)/2;
            case STEP -> groupVolume*.15f;
            case LAND -> groupVolume*.5f;
        };
    }
    public float pitch(float groupPitch) {
        return groupPitch * switch(type) { case BREAK, PLACE -> .8f; case STEP -> 1f; case LAND -> .75f; };
    }
}
