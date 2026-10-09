"""Niagara discovery must preserve custom assets and report missing templates."""
import types
import unittest
import setup_realistic_physics as setup


class System:
    def get_path_name(self): return '/NiagaraFluids/Template.Template'


class NiagaraSetup(unittest.TestCase):
    def make(self, existing=None, templates=()):
        warnings=[];copies=[];saved=[]
        assets=types.SimpleNamespace(does_asset_exist=lambda path:existing is not None,
            duplicate_asset=lambda source,target:copies.append((source,target)) or System(),
            save_loaded_asset=lambda asset,force:saved.append(asset) or True)
        unreal=types.SimpleNamespace(EditorAssetLibrary=assets,NiagaraSystem=System,
            load_asset=lambda path:existing,
            AssetRegistryHelpers=types.SimpleNamespace(get_asset_registry=lambda:types.SimpleNamespace(get_assets_by_path=lambda *args,**kwargs:templates)),
            log=lambda text:None,log_warning=warnings.append)
        return unreal,warnings,copies,saved

    def test_missing_template_reports_fallback(self):
        unreal,warnings,copies,saved=self.make()
        self.assertIsNone(setup.setup_realistic_niagara(unreal))
        self.assertEqual(len(warnings),1);self.assertIn('not a volumetric',warnings[0]);self.assertFalse(copies)

    def test_custom_system_preserved(self):
        system=System();unreal,_,copies,_=self.make(system)
        self.assertIs(setup.setup_realistic_niagara(unreal),system);self.assertFalse(copies)

    def test_only_known_explosion_system_is_copied(self):
        template=types.SimpleNamespace(asset_name='Grid3D_Gas_Explosion',get_asset=lambda:System())
        unrelated=types.SimpleNamespace(asset_name='UnrelatedExplosion',get_asset=lambda:System())
        unreal,_,copies,saved=self.make(templates=[unrelated,template])
        self.assertIsInstance(setup.setup_realistic_niagara(unreal),System)
        self.assertEqual(copies,[('/NiagaraFluids/Template.Template','/Game/Bridge/Realistic/NS_RealisticExplosion')]);self.assertEqual(len(saved),1)

    def test_foreign_asset_at_destination_rejected(self):
        unreal,_,copies,_=self.make(existing=object())
        with self.assertRaisesRegex(RuntimeError,'non-Niagara'):setup.setup_realistic_niagara(unreal)
        self.assertFalse(copies)

    def test_water_missing_never_uses_pool_or_gas_fallback(self):
        templates=[types.SimpleNamespace(asset_name=n,get_asset=lambda:System())
                   for n in ('Grid3D_FLIP_Pool','Grid3D_Gas_Explosion','UnrelatedHose')]
        unreal,warnings,copies,_=self.make(templates=templates)
        self.assertIsNone(setup.setup_realistic_water(unreal))
        self.assertFalse(copies)
        self.assertIn('Water buckets remain unused',warnings[0])

    def test_only_continuous_flip_hose_is_cloned_for_water(self):
        template=types.SimpleNamespace(asset_name='Grid3D_FLIP_Hose',get_asset=lambda:System())
        unreal,_,copies,saved=self.make(templates=[template])
        self.assertIsInstance(setup.setup_realistic_water(unreal),System)
        self.assertEqual(copies,[('/NiagaraFluids/Template.Template','/Game/Bridge/Realistic/NS_RealisticWater')])
        self.assertEqual(len(saved),1)

    def test_custom_water_asset_is_preserved(self):
        system=System();unreal,_,copies,_=self.make(system)
        self.assertIs(setup.setup_realistic_water(unreal),system)
        self.assertFalse(copies)

    def test_foreign_water_asset_is_rejected(self):
        unreal,_,copies,_=self.make(existing=object())
        with self.assertRaisesRegex(RuntimeError,'non-Niagara'):
            setup.setup_realistic_water(unreal)
        self.assertFalse(copies)


if __name__ == '__main__':unittest.main()
