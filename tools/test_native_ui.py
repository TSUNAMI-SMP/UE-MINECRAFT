"""Exercise UI package validation before it can modify the Unreal asset registry."""
import copy
import hashlib
import importlib.util
import json
import pathlib
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("import_minecraft_ui", pathlib.Path(__file__).with_name("import_minecraft_ui.py"))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


def png(width=2, height=2):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    scanlines = b"".join(b"\0" + b"\xff\xff\xff\xff" * width for _ in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")


class NativeUiValidation(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        self.data = png()
        (self.root / "icon.png").write_bytes(self.data)
        self.entry = dict(file="icon.png", sha256=hashlib.sha256(self.data).hexdigest(), width=2, height=2)
        self.manifest = dict(kind="native-ui", version=1, sprites={"hud/hotbar": dict(self.entry)}, items=[dict(id="minecraft:stone", name="石", icon="icon.png", sha256=self.entry["sha256"], width=2, height=2, maxCount=64, block="minecraft:stone", modelKey="")], font=dict(self.entry, glyphs=[dict(codepoint=65, x=0, y=0, width=2, height=2, advance=6, drawWidth=1, drawHeight=1, ascent=7)]))

    def load(self, manifest=None):
        path = self.root / "manifest.json"
        path.write_text(json.dumps(manifest or self.manifest), encoding="utf-8")
        return ui.load_ui_manifest(path)

    def test_valid_japanese_name_and_scaled_font(self):
        result = self.load()
        self.assertEqual(result["items"][0]["name"], "石")
        self.assertEqual(result["font"]["glyphs"][0]["drawWidth"], 1)
        self.assertEqual(result["items"][0]["source"], str(self.root / "icon.png"))

    def test_ue_only_namespace_alias_retains_local_icon_and_model(self):
        item=copy.deepcopy(self.manifest["items"][0])
        item.update(id="uebridge:realistic_sand",name="リアリスティック砂",modelKey="minecraft:sand")
        self.manifest["items"].append(item)
        result=self.load()
        self.assertEqual(result["items"][1]["modelKey"],"minecraft:sand")
        self.assertEqual(result["items"][0]["source"],result["items"][1]["source"])

    def test_weapon_attributes_are_optional_but_must_be_paired_and_finite(self):
        item=self.manifest["items"][0]
        item.update(attackDamage=7,attackSpeed=1.6)
        self.assertEqual(1.6,self.load()["items"][0]["attackSpeed"])
        for damage,speed in ((-1,1.6),(7,0),(7,float("nan")),(7,True),(float("inf"),1.6)):
            item.update(attackDamage=damage,attackSpeed=speed)
            with self.assertRaisesRegex(ValueError,"weapon attributes"):
                self.load()
        item.pop("attackSpeed")
        with self.assertRaisesRegex(ValueError,"weapon attributes"):
            self.load()

    def gameplay(self):
        return dict(version=1,tickRate=20,randomTickSpeed=3,recipes=[dict(id="minecraft:stone_recipe",type="minecraft:crafting_shaped",width=1,height=1,ingredients=[["minecraft:stone","minecraft:dirt"]],result="minecraft:stone",count=1)],fuels={"minecraft:coal":1600},remainders={"minecraft:lava_bucket":"minecraft:bucket"})

    def test_gameplay_registry_retains_resolved_tags_fuel_and_remainders(self):
        self.manifest["gameplay"]=self.gameplay()
        result=self.load()["gameplay"]
        self.assertEqual(["minecraft:stone","minecraft:dirt"],result["recipes"][0]["ingredients"][0])
        self.assertEqual(1600,result["fuels"]["minecraft:coal"])
        self.assertEqual("minecraft:bucket",result["remainders"]["minecraft:lava_bucket"])

    def test_invalid_recipe_layout_and_duplicate_ids_rejected(self):
        self.manifest["gameplay"]=self.gameplay()
        recipe=self.manifest["gameplay"]["recipes"][0]
        recipe["width"]=2
        with self.assertRaisesRegex(ValueError,"shaped recipe grid"): self.load()
        recipe["width"]=1
        self.manifest["gameplay"]["recipes"].append(dict(recipe))
        with self.assertRaisesRegex(ValueError,"duplicate native recipe"): self.load()

    def test_invalid_gameplay_clock_and_fuel_are_rejected(self):
        for field,value in (("version",True),("randomTickSpeed",-1),("randomTickSpeed",float('nan')),("tickRate",30)):
            self.manifest["gameplay"]=self.gameplay();self.manifest["gameplay"][field]=value
            with self.assertRaisesRegex(ValueError,"native.*(?:version|tick|rate)"): self.load()
        self.manifest["gameplay"]=self.gameplay();self.manifest["gameplay"]["fuels"]["minecraft:coal"]=True
        with self.assertRaisesRegex(ValueError,"fuel/remainder"): self.load()

    def test_container_layouts_counts_and_coordinates_validated(self):
        self.manifest["gameplay"]=self.gameplay()
        for kind,size in (("chest",27),("furnace",3),("hopper",5),("dropper",9),("dispenser",9)):
            container=dict(key="-8,64,9",kind=kind,slots=[dict(item="",count=0) for _ in range(size)],burn=0,burnTotal=0,cook=0)
            container["slots"][0]=dict(item="minecraft:stone",count=32)
            self.manifest["gameplay"]["containers"]=[container]
            self.assertEqual(32,self.load()["gameplay"]["containers"][0]["slots"][0]["count"])
            container["slots"][0]["count"]=100
            with self.assertRaisesRegex(ValueError,"container stack"): self.load()
            container["slots"][0]["count"]=32;container["slots"].pop()
            with self.assertRaisesRegex(ValueError,"container slots"): self.load()
        self.manifest["gameplay"]["containers"]=[dict(key="40000000,0,0",kind="furnace",slots=[dict(item="",count=0)]*3,burn=0,burnTotal=0,cook=0)]
        with self.assertRaisesRegex(ValueError,"world bounds"): self.load()

    def test_armor_sprite_and_default_glint_use_verified_assets(self):
        self.manifest["sprites"]["equipment/minecraft/iron_chestplate"]=dict(self.entry)
        item=self.manifest["items"][0];item.update(armorSprite="equipment/minecraft/iron_chestplate",glint=True)
        self.assertTrue(self.load()["items"][0]["glint"])
        item["armorSprite"]="equipment/missing/iron_chestplate"
        with self.assertRaisesRegex(ValueError,"armor sprite"): self.load()

    def test_particle_frame_order_repeats_and_asset_validation(self):
        self.manifest["sprites"]["particle/generic_7"]=dict(self.entry)
        self.manifest["sprites"]["particle/generic_6"]=dict(self.entry)
        frames=["particle/generic_7","particle/generic_7","particle/generic_6"]
        self.manifest["particleFrames"]={"minecraft:smoke":frames}
        self.assertEqual(frames,self.load()["particleFrames"]["minecraft:smoke"])
        for invalid in ({"minecraft:smoke":["particle/missing"]},{"bad id":frames},{"minecraft:smoke":True},{"minecraft:smoke":frames*100}):
            self.manifest["particleFrames"]=invalid
            with self.assertRaisesRegex(ValueError,"particle frame"): self.load()

    def creative_group(self):
        return dict(id="minecraft:building_blocks", name="建築ブロック", type="category", icon="minecraft:stone",
                    texture="hud/hotbar", row=0, column=0, special=False, scrollbar=True, renderName=True, items=["minecraft:stone"])

    def test_creative_group_order_is_preserved_and_legacy_is_optional(self):
        self.assertEqual([], self.load()["groups"])
        second = dict(self.manifest["items"][0], id="minecraft:dirt", name="土")
        self.manifest["items"].append(second)
        group = self.creative_group()
        group["items"] = ["minecraft:dirt", "minecraft:stone"]
        self.manifest["groups"] = [group]
        self.assertEqual(["minecraft:dirt", "minecraft:stone"], self.load()["groups"][0]["items"])

    def test_creative_group_unknown_duplicate_and_missing_assets_are_rejected(self):
        self.manifest["groups"] = [self.creative_group()]
        for order in (["minecraft:unexported"], ["minecraft:stone", "minecraft:stone"]):
            self.manifest["groups"][0]["items"] = order
            with self.assertRaisesRegex(ValueError, "item order"):
                self.load()
        self.manifest["groups"][0] = self.creative_group()
        self.manifest["groups"][0]["texture"] = "missing/panel"
        with self.assertRaisesRegex(ValueError, "icon/texture"):
            self.load()

    def test_creative_group_positions_are_unique_and_flags_are_boolean(self):
        self.manifest["groups"] = [self.creative_group()]
        self.manifest["groups"].append(dict(self.creative_group(), id="minecraft:natural_blocks"))
        with self.assertRaisesRegex(ValueError, "position"):
            self.load()
        self.manifest["groups"] = [self.creative_group()]
        for row, column in ((True, 0), (0, 7), (-1, 0)):
            self.manifest["groups"][0].update(row=row, column=column)
            with self.assertRaisesRegex(ValueError, "position"):
                self.load()
        self.manifest["groups"][0] = self.creative_group()
        self.manifest["groups"][0]["special"] = 1
        with self.assertRaisesRegex(ValueError, "flags"):
            self.load()

    def test_equipment_component_attributes_are_validated(self):
        item=self.manifest["items"][0]
        item.update(equipmentSlot=2,armor=8,armorToughness=3,armorKnockbackResistance=.1)
        self.assertEqual(2,self.load()["items"][0]["equipmentSlot"])
        for slot in (True,-1,5):
            item["equipmentSlot"]=slot
            with self.assertRaisesRegex(ValueError,"equipment slot"):
                self.load()
        item["equipmentSlot"]=2
        for key,value in (("armor",float("nan")),("armorToughness",-1),("armorKnockbackResistance",2)):
            good=item[key];item[key]=value
            with self.assertRaisesRegex(ValueError,"equipment attributes"):
                self.load()
            item[key]=good

    def test_death_poof_order_duplicates_and_legacy_are_preserved(self):
        self.assertEqual([],self.load()["deathPoofFrames"])
        self.manifest["sprites"]["particle/generic_0"]=dict(self.entry)
        self.manifest["sprites"]["pack:particle/replacement"]=dict(self.entry)
        order=["pack:particle/replacement","particle/generic_0","pack:particle/replacement"]
        self.manifest["deathPoofFrames"]=order
        self.assertEqual(order,self.load()["deathPoofFrames"])
        self.manifest["deathPoofFrames"]=["missing/sprite"]
        with self.assertRaisesRegex(ValueError,"death poof"):
            self.load()

    def test_modified_texture_rejected(self):
        (self.root / "icon.png").write_bytes(self.data + b"tampered")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.load()

    def test_escaping_path_rejected(self):
        self.manifest["items"][0]["icon"] = "../icon.png"
        with self.assertRaisesRegex(ValueError, "relative PNG"):
            self.load()

    def test_symlink_escape_rejected(self):
        with tempfile.TemporaryDirectory() as outside:
            external = pathlib.Path(outside) / "asset.png"
            external.write_bytes(self.data)
            (self.root / "escape.png").symlink_to(external)
            self.manifest["items"][0]["icon"] = "escape.png"
            with self.assertRaisesRegex(ValueError, "escaping"):
                self.load()

    def test_duplicate_items_rejected(self):
        self.manifest["items"].append(dict(self.manifest["items"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate native UI item"):
            self.load()

    def test_spawn_entity_uses_exported_id(self):
        self.manifest["items"][0]["spawnType"] = "custom:entity/special"
        self.assertEqual(self.load()["items"][0]["spawnType"], "custom:entity/special")

    def test_invalid_spawn_entity_rejected(self):
        self.manifest["items"][0]["spawnType"] = "not an entity"
        with self.assertRaisesRegex(ValueError, "spawn entity"):
            self.load()

    def test_resource_pack_namespaced_sprite_is_retained(self):
        self.manifest['sprites']['my_pack:hud/custom'] = dict(self.entry)
        self.assertIn('my_pack:hud/custom', self.load()['sprites'])

    def test_current_celestial_sprites_keep_phase_keys_and_verified_pixels(self):
        phases = ('full_moon', 'waning_gibbous', 'third_quarter', 'waning_crescent',
                  'new_moon', 'waxing_crescent', 'first_quarter', 'waxing_gibbous')
        keys = ['environment/celestial/sun'] + ['environment/celestial/moon/' + phase for phase in phases]
        for key in keys:
            self.manifest['sprites'][key] = dict(self.entry)
        result = self.load()
        for key in keys:
            self.assertEqual(result['sprites'][key]['sha256'], self.entry['sha256'])
            self.assertEqual(pathlib.Path(result['sprites'][key]['source']).read_bytes(), self.data)

    def test_malformed_sprite_namespace_is_rejected(self):
        self.manifest['sprites']['my_pack:other:hud/custom'] = dict(self.entry)
        with self.assertRaisesRegex(ValueError, 'sprite ID'):
            self.load()

    def test_glyph_outside_atlas_rejected(self):
        self.manifest["font"]["glyphs"][0]["x"] = 1
        with self.assertRaisesRegex(ValueError, "atlas bounds"):
            self.load()

    def test_nan_glyph_metrics_rejected(self):
        self.manifest["font"]["glyphs"][0]["advance"] = float("nan")
        with self.assertRaisesRegex(ValueError, "advance"):
            self.load()

    def test_unsupported_font_allowed_with_explicit_fallback(self):
        self.manifest.pop("font")
        self.assertNotIn("font", self.load())

    def test_crc_rejected_even_with_updated_hash(self):
        damaged = bytearray(self.data)
        damaged[29] ^= 1
        (self.root / "icon.png").write_bytes(damaged)
        self.manifest["sprites"]["hud/hotbar"]["sha256"] = hashlib.sha256(damaged).hexdigest()
        with self.assertRaisesRegex(ValueError, "chunk"):
            self.load()


if __name__ == "__main__":
    unittest.main()
