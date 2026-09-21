import unittest,sys,pathlib,json,tempfile,hashlib,subprocess,os
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
        root=pathlib.Path(__file__).resolve().parents[1]
        path=root/'data/database.json'
        if not path.exists():self.skipTest('generate data first')
        db=json.loads(path.read_text(encoding='utf-8'));counts=validate(db)
        report=json.loads((root/'data/generation-report.json').read_text())
        self.assertEqual(counts,report['source_counts'])
        self.assertEqual(counts,{'uniques':1413,'sets':455,'runewords':211,'bases':520})
        self.assertEqual(len({r['id'] for r in db['records']}),len(db['records']))
        self.assertTrue(all('file' in r['source_ref'] and 'row' in r['source_ref'] for r in db['records']))
        self.assertEqual(report['sha256'],hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(report['properties'],sum(len(r['properties']) for r in db['records']))
        self.assertEqual(report['numeric_properties'],sum(p['min_value'] is not None for r in db['records'] for p in r['properties']))
        for r in db['records']:
            for p in r['properties']:
                if p['raw'].get('itemsRequired') or p['raw'].get('fullSet'):self.assertTrue(p['conditional'])
    def test_validation_rejects_malformed_data(self):
        valid={'schema_version':1,'provenance':{},'records':[{
            'id':'u:test','tab':'uniques','name':'Test','fields':{},'numbers':{},
            'properties':[],'display_lines':['Test'],'source_ref':{'file':'uniques.json','row':0},
            'search_text':'test'}]}
        # A valid database must represent all four catalogs.
        with self.assertRaisesRegex(ValueError,'missing catalog tabs'):validate(valid)
        root=pathlib.Path(__file__).resolve().parents[1]
        db=json.loads((root/'data/database.json').read_text(encoding='utf-8'))
        broken=json.loads(json.dumps(db));broken['records'][0]['properties'][0]['min_value']=100;broken['records'][0]['properties'][0]['max_value']=1
        with self.assertRaisesRegex(ValueError,'invalid property range'):validate(broken)
        broken=json.loads(json.dumps(db));broken['records'][0]['numbers']['required_level']='one'
        with self.assertRaisesRegex(ValueError,'invalid numbers'):validate(broken)
        broken=json.loads(json.dumps(db));broken['records'][1]['id']=broken['records'][0]['id']
        with self.assertRaisesRegex(ValueError,'duplicate id'):validate(broken)
    def test_catalog_semantics(self):
        root=pathlib.Path(__file__).resolve().parents[1]
        path=root/'data/database.json'
        if not path.exists():self.skipTest('generate data first')
        db=json.loads(path.read_text(encoding='utf-8'))
        by_tab={tab:[r for r in db['records'] if r['tab']==tab] for tab in ('uniques','sets','runewords','bases')}
        self.assertTrue(all(r['fields'].get('tier') for r in by_tab['bases']))
        self.assertTrue(all(r['numbers'].get('rune_count')==len(r['fields'].get('runes',[])) for r in by_tab['runewords']))
        self.assertTrue(all(r['fields'].get('compatible_base') for r in by_tab['runewords']))
        self.assertTrue(any(p['property_id']=='ias' and p['min_value']!=p['max_value'] for r in by_tab['uniques'] for p in r['properties'] if p['min_value'] is not None))
        self.assertTrue(any(p['conditional'] for r in by_tab['sets'] for p in r['properties']))
        doom=[r for r in by_tab['runewords'] if r['source_ref']['index']=='Doom1']
        self.assertEqual(len(doom),2)
        self.assertEqual(len({r['id'] for r in doom}),2)
        self.assertEqual({tuple(r['fields']['runes']) for r in doom},{('Hel','Ohm','Um','Lo','Cham')})
    def test_deterministic_regeneration(self):
        root=pathlib.Path(__file__).resolve().parents[1]
        lock=json.loads((root/'upstream.lock.json').read_text())
        source=pathlib.Path(os.environ.get('ITEMDB_SOURCE',root/'.deps/website-data'/lock['website']['sha']))
        if not source.exists():self.skipTest('cached pinned source is not present')
        with tempfile.TemporaryDirectory() as temporary:
            one=pathlib.Path(temporary)/'one/database.json';two=pathlib.Path(temporary)/'two/database.json'
            command=[sys.executable,str(root/'tools/generate_database.py'),'--source',str(source),'--output']
            subprocess.run(command+[str(one)],check=True,capture_output=True,text=True)
            subprocess.run(command+[str(two)],check=True,capture_output=True,text=True)
            self.assertEqual(one.read_bytes(),two.read_bytes())
if __name__=='__main__':unittest.main()
