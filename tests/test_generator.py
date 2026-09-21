import unittest,sys,pathlib,json,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from generate_database import fmt,Normalizer,validate
class GeneratorTests(unittest.TestCase):
    def test_roll(self):self.assertEqual(fmt('%+d%% Enhanced Damage',[150,250]),'+150-250% Enhanced Damage')
    def test_skill_range(self):self.assertEqual(fmt('Level %d %s Aura',[10,15,'Fanaticism']),'Level 10-15 Fanaticism Aura')
    def test_proc(self):self.assertEqual(fmt('%d%% level %d %s',[5,10,'Fireball']),'5% level 10 Fireball')
    def test_index(self):self.assertEqual(fmt('%0%% Reanimate: %1',[5,10,'Skeleton']),'5-10% Reanimate: Skeleton')
    def test_negative(self):self.assertEqual(fmt('%+d%%',[-10,-5]),'-10--5%')
    def test_fraction(self):self.assertEqual(fmt('%d',[2.5]),'2.5')
    def test_nested(self):
        n=object.__new__(Normalizer);n.strings={'x':'%d life'};n.warnings={}
        flat=list(n.flatten([{'key':'','pickMode':'one','children':[{'key':'x','args':[10]}]}]))
        self.assertTrue(flat[0][1]);self.assertIn('group selection one',flat[0][2])
    def test_conditional_numeric(self):
        n=object.__new__(Normalizer);n.strings={'proc':'%d%% chance to cast level %d %s','ModStr4m':'%+d%% Increased Attack Speed'};n.catalog={}
        p=n.property({'key':'proc','args':[5,10,'skill']},False,'proc')
        self.assertIsNone(p['min_value'])
        p=n.property({'key':'ModStr4m','args':[20,40]},False,'ias')
        self.assertEqual((p['property_id'],p['min_value'],p['max_value']),('ias',20,40))
    def test_generated(self):
        path=pathlib.Path(__file__).resolve().parents[1]/'data/database.json'
        if not path.exists():self.skipTest('generate data first')
        db=json.loads(path.read_text(encoding='utf-8'));counts=validate(db)
        self.assertGreater(counts['uniques'],1000)
        for r in db['records']:
            for p in r['properties']:
                if p['raw'].get('itemsRequired') or p['raw'].get('fullSet'):self.assertTrue(p['conditional'])
if __name__=='__main__':unittest.main()
