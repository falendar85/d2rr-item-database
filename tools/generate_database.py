"""Rebuild offline catalog from a pinned website checkout or downloaded keyed JSON.

No runtime scraping. No writes to game/mod/save directories. Python 3.11+ stdlib.
"""
import argparse, collections, hashlib, json, math, pathlib, re, urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[1]
FILES=['keyed/'+n+'.json' for n in ['uniques','sets','runewords','armors','weapons','ias-calculator']]+['strings/enUS.json']
TOKEN=re.compile(r'%(?:\+d|[dDsSi]|c\d|\d|%)')
# Canonical aliases are deliberately keyed by the source identifier, not English text.
ALIASES={
 'ModStr4m':'ias','ModStr4v':'fcr','ModStr4p':'fhr','ModStr4s':'frw',
 'ModStr3k':'all_skills','strModEnhancedDamage':'enhanced_weapon_damage','Modstr2v':'enhanced_defense',
 'ModStr2z':'life_leech','ModStr2y':'mana_leech','ModStr5c':'crushing_blow','ModStr5q':'deadly_strike',
 'ModStr3m':'open_wounds','ModStr5z':'cannot_be_frozen','ModStr2uPercent':'physical_damage_reduction',
 'ModStr2u':'physical_damage_reduction_flat','ModStr2t':'magic_damage_reduction',
 'strModAllResistances':'all_resistances','ModStr1j':'fire_resistance','ModStr1k':'cold_resistance',
 'ModStr1l':'lightning_resistance','ModStr1n':'poison_resistance','ModStr1m':'magic_resistance',
 'ModitemAura':'aura','ItemModifierNonClassSkill':'oskill','strSocketedCount':'sockets',
 'ModStr1x':'magic_find','ModStr3y':'ignore_target_defense','ModStr5o':'target_defense_reduction',
 'ModStr1h':'attack_rating','ModStr4c':'attack_rating_percent','ModStr4e':'damage_to_demons','ModStr4f':'damage_to_undead',
 'ModStr1u':'life','ModStr1e':'mana','ModStr1a':'strength','ModStr1b':'dexterity',
 'ModStr1c':'vitality','ModStr1d':'energy','ModStr1i':'defense',
 'Moditemenresfiresk':'enemy_fire_resistance_reduction','Moditemenrescoldsk':'enemy_cold_resistance_reduction',
 'Moditemenresltngsk':'enemy_lightning_resistance_reduction','Moditemenrespoissk':'enemy_poison_resistance_reduction',
 'ModitemdamFiresk':'fire_skill_damage','ModitemdamColdsk':'cold_skill_damage','ModitemdamLtngsk':'lightning_skill_damage','ModitemdamPoissk':'poison_skill_damage',
 'strModFireDamageRange':'fire_damage','strModColdDamageRange':'cold_damage','strModLightningDamageRange':'lightning_damage',
 'strModPoisonDamageRange':'poison_damage','strModMinDamageRange':'physical_damage',
}
for k in ['ModStr3a','ModStr3b','ModStr3c','ModStr3d','ModStr3e','ModStre8a','ModStre8b']:
    ALIASES[k]='class_skills'

def clean(s):
    s=re.sub(r'ÿc.', '',str(s))
    s=re.sub(r'\[/?[mf]\]', '',s, flags=re.I)
    return s.strip()
def fmt(template,args):
    """Resolve D2 positional placeholders, including surplus numeric roll pairs."""
    tokens=TOKEN.findall(template)
    sequential=[x for x in tokens if x!='%%' and not re.fullmatch(r'%[0-9]|%c[0-9]',x)]
    indexed=[int(x[1]) for x in tokens if re.fullmatch(r'%\d',x)]
    slots=max(indexed)+1 if indexed else len(sequential)
    budget=max(0,len(args)-slots)
    pos=0; resolved=[]
    def number(x): return format(x,'.8g') if isinstance(x,(int,float)) else str(x)
    if indexed:
        for _ in range(slots):
            if pos>=len(args):resolved.append('?');continue
            a=args[pos];pos+=1
            if budget and isinstance(a,(int,float)) and pos<len(args) and isinstance(args[pos],(int,float)):
                b=args[pos];pos+=1;budget-=1;resolved.append(number(a) if a==b else number(a)+'-'+number(b))
            else:resolved.append(number(a))
    pos=0
    def replace(m):
        nonlocal pos,budget
        token=m.group()
        if token=='%%':return '%'
        if token.startswith('%c'):return ''
        if indexed and re.fullmatch(r'%\d',token):return resolved[int(token[1])]
        if pos>=len(args):return token
        a=args[pos];pos+=1
        text=number(a)
        if token=='%+d' and isinstance(a,(int,float)) and a>=0:text='+'+text
        if budget and token in ('%d','%D','%i','%+d') and isinstance(a,(int,float)) and pos<len(args) and isinstance(args[pos],(int,float)):
            b=args[pos];pos+=1;budget-=1
            if a!=b:text+='-'+number(b)
        return text
    return clean(TOKEN.sub(replace,template))

class Normalizer:
    def __init__(self,source):
        self.source=pathlib.Path(source)
        self.strings=self.read('strings/enUS.json')
        self.armor_rows=self.read('keyed/armors.json')
        self.weapon_rows=self.read('keyed/weapons.json')
        self.base_rows=self.armor_rows+self.weapon_rows
        self.bases={b['NameKey']:b for b in self.base_rows}
        self.speed={b['Code']:b for b in self.read('keyed/ias-calculator.json')['Weapons']}
        self.catalog={};self.warnings=collections.Counter()
    def read(self,p):return json.loads((self.source/p).read_text(encoding='utf-8-sig'))
    def t(self,s):return clean(self.strings.get(str(s),str(s)))
    def line(self,p):
        key=p.get('key','');template=self.t(key)
        args=[self.t(a) if isinstance(a,str) else a for a in p.get('args',[])]
        if key=='healperhit' and args:template='%d '+template
        if key=='strSkillRandomFromSkillClass' and len(args)>=2:
            if isinstance(args[0],(int,float)) and isinstance(args[1],(int,float)):
                args=[str(args[0]) if args[0]==args[1] else f'{args[0]}-{args[1]}']+args[2:]
            if len(args)<3:template=re.sub(r'\s*%s\s*$','',template)
        text=fmt(template,args)
        if key not in self.strings:
            self.warnings['unresolved_string_keys']+=1
            text=key+(' '+json.dumps(args,ensure_ascii=False) if args else '')
        if p.get('perLevel'):text+=' (per character level)'
        if p.get('qualifier'):text+=' ('+p['qualifier']+')'
        if p.get('classOnly'):text+=' '+self.t(p['classOnly'])
        if p.get('itemsRequired'):text+=f" ({p['itemsRequired']} set pieces)"
        if p.get('fullSet'):text+=' (full set)'
        return text
    def flatten(self,lines,conditional=False,label=''):
        for p in lines:
            cond=conditional or bool(p.get('chance') is not None or p.get('pickMode') or p.get('itemsRequired') or p.get('fullSet'))
            text=self.line(p) if p.get('key') else ''
            tag=label
            if p.get('chance') is not None:tag+=f" [group chance {p['chance']}%]"
            if p.get('pickMode'):tag+=f" [group selection {p['pickMode']}]"
            if text:yield p,cond,(tag+' '+text).strip()
            yield from self.flatten(p.get('children',[]),cond,tag)
    def property(self,p,conditional,text):
        key=p['key'];pid=ALIASES.get(key,key.lower())
        if key.startswith('ItemExpansiveChanc') or key.startswith('Moditemskon'):pid='chance_to_cast'
        if key.startswith('StrSklTabItem'):pid='skill_tree'
        if 'SingleSkill' in key:pid='individual_skill'
        template=self.t(key);args=p.get('args',[])
        numbers=[a for a in args if isinstance(a,(float,int)) and not isinstance(a,bool)]
        tokens=[t for t in TOKEN.findall(template) if t not in ('%%',) and not t.startswith('%c')]
        numeric_slots=sum(t in ('%d','%D','%i','%+d') for t in tokens)
        # Only unambiguous scalar/range values are eligible for numeric comparisons.
        lo=hi=None
        if numeric_slots==1 and len(numbers) in (1,2):lo=min(numbers);hi=max(numbers)
        if not args and '%' not in template:lo=hi=1
        entry={'property_id':pid,'canonical_name':pid.replace('_',' ').title() if key in ALIASES else template,
               'min_value':lo,'max_value':hi,'unit':'percent' if '%%' in template else 'number',
               'text':text,'conditional':conditional,'per_level':bool(p.get('perLevel')),
               'scope':p.get('qualifier',''),'raw':p}
        self.catalog.setdefault(pid,{'id':pid,'label':entry['canonical_name'],'numeric':False,'source_keys':[]})
        meta=self.catalog[pid];meta['numeric']|=lo is not None
        if key not in meta['source_keys']:meta['source_keys'].append(key)
        return entry
    def tier(self,b):
        code=b.get('NameKey','')
        for key,name in [('NormCode','Normal'),('UberCode','Exceptional'),('UltraCode','Elite')]:
            if code and code==b.get(key):return name
        return 'Other'
    def equipment(self,e,numbers):
        # All damage modes remain in display_lines. Sort by first physical mode.
        for d in e.get('DamageTypes',[]):
            if d.get('Type') in (0,1,2,3) and 'AverageDamage' in d:
                numbers['avg_damage']=d['AverageDamage']
                if d.get('Lines'):
                    a=d['Lines'][0].get('args',[])
                    if len(a)==2:numbers.update(min_damage=a[0],max_damage=a[1])
                    elif len(a)==4:numbers.update(min_damage=(a[0]+a[1])/2,max_damage=(a[2]+a[3])/2)
                break
        for p in e.get('Lines',[]):
            a=p.get('args',[]);key=p.get('key')
            if key in ('strRequiredStrength','strRequiredDexterity') and a:numbers['strength' if key=='strRequiredStrength' else 'dexterity']=a[0]
            if key in ('strDefense','strDefenseRange','strDefenseRangeRange') and a:numbers['defense']=sum(a)/len(a)
    def record(self,row,tab,index,setrow=None):
        code=row.get('Code') or row.get('NameKey','');b=self.bases.get(code,{})
        e=row if tab=='bases' else row.get('Equipment',{})
        name=self.t(row.get('Index',row.get('NameKey','Unknown')))
        typ=row.get('Type',b.get('Type',{}));typ=typ if isinstance(typ,str) else typ.get('Index',typ.get('Name',''))
        types=[self.t(typ)] if typ else []
        if tab=='runewords':types=[self.t(t.get('Index',t.get('Name','')) if isinstance(t,dict) else t) for t in row.get('Types',[])]
        category={0:'Armor',1:'Weapon',2:'Other'}.get(e.get('EquipmentType'),'Other')
        fields={'name':[name],'base':[self.t(e.get('NameKey',code))],'base_code':[code],
                'type':types,'weapon_type':types if category=='Weapon' else [],'category':[category],
                'class':[self.t(e.get('RequiredClass') or b.get('RequiredClass',''))],
                'tier':[self.tier(row if tab=='bases' else b)],
                'origin':[{'Y':'Vanilla','N':'Reimagined'}.get(row.get('Vanilla'),'Unknown')],
                'set':[self.t(row.get('SetName',''))]}
        if tab=='bases':
            family_codes=list(dict.fromkeys(row.get(k,'') for k in ('NormCode','UberCode','UltraCode') if row.get(k,'')))
            fields['base_family_code']=[family_codes[0] if family_codes else code]
            fields['base_family']=[self.t(self.bases[c].get('NameKey',c)) for c in family_codes if c in self.bases]
        nums={'required_level':row.get('RequiredLevel',0)}
        self.equipment(e,nums)
        # Omitted requirements are zero in keyed equipment, but unknown base metadata remains missing.
        nums.setdefault('strength',0);nums.setdefault('dexterity',0)
        if code in self.speed:nums['weapon_speed']=self.speed[code]['Speed']
        sockets=(row if tab=='bases' else b).get('GemSockets')
        if isinstance(sockets,str):
            caps=[int(v) for v in re.findall(r':\s*(\d+)',sockets)]
            if len(caps)==3:nums.update(zip(['sockets_low','sockets_mid','sockets_high'],caps));nums['max_sockets']=max(caps)
        elif isinstance(sockets,(int,float)):nums['max_sockets']=sockets
        if tab=='runewords':
            # The English export currently renders "Jah Rune (#31)". Store the
            # canonical rune name so `runes=jah` works and keep order intact.
            fields['runes']=[re.sub(r'\s+Rune(?:\s*\(#\d+\))?$','',self.t(r['NameKey']),flags=re.I) for r in row.get('Runes',[])]
            nums['rune_count']=nums['sockets']=len(fields['runes'])
            # Resolve broad accepted types to actual compatible weapon/base types from the same export.
            accepted={t.get('Index','') for t in row.get('Types',[]) if isinstance(t,dict)}
            compatible=[]
            class_types={'Amazon':'amazitype','Barbarian':'barbitype','Necromancer':'necritype',
                         'Paladin':'palaitype','Sorceress':'sorcitype','Assassin':'assnitype',
                         'Druid':'druiitype','Warlock':'warlitype'}
            for base in self.base_rows:
                bt=base.get('Type',{}).get('Index','')
                chain={bt}|{t+'itype' for t in self.speed.get(base['NameKey'],{}).get('Types',[])}
                required_class=base.get('RequiredClass','')
                if required_class in class_types:chain.add(class_types[required_class])
                if base.get('EquipmentType')==0:chain.add('armoitype')
                if bt in ('shieitype','ashditype','headitype','grimitype'):chain.add('shlditype')
                if chain&accepted:
                    compatible.append(self.t(base['NameKey']))
                    types.append(self.t(bt))
            fields['compatible_base']=compatible;fields['type']=list(dict.fromkeys(types))
            fields['category']=['Runeword']
        lines=[name]
        if fields['base'][0] and tab!='runewords':lines.append(fields['base'][0])
        if tab=='sets':lines.append('Set: '+fields['set'][0])
        lines.append(f"Required Level: {nums['required_level']}")
        if tab=='runewords':lines+=['Runes: '+' + '.join(fields['runes']),'Allowed types: '+', '.join(self.t(t.get('Index',t.get('Name',''))) for t in row.get('Types',[]))]
        if sockets is not None:lines.append('Base socket capacity by item level: '+str(sockets))
        if 'weapon_speed' in nums:lines.append('Base weapon speed: '+str(nums['weapon_speed']))
        for d in e.get('DamageTypes',[]):lines.extend(text for _,_,text in self.flatten(d.get('Lines',[])))
        lines.extend(text for _,_,text in self.flatten(e.get('Lines',[])))
        properties=[]
        if tab!='bases':
            for p,c,t in self.flatten(row.get('Lines',[])):properties.append(self.property(p,c,t));lines.append(t)
        # Base automagic is conditional; include the group level in the detail, never silently guarantee it.
        for g in row.get('AutoMagicGroups',[]) if tab=='bases' else []:
            for p,c,t in self.flatten(g.get('Lines',[]),True,f"Automagic {self.t(g.get('NameKey',''))} (level {g.get('Level','?')}):"):
                properties.append(self.property(p,c,t));lines.append(t)
        if setrow:
            for label,ps in [('Item set bonus',sum(row.get('SetBonuses',[]),[])),('Partial set bonus',setrow.get('PartialBonuses',[])),('Full set bonus',setrow.get('FullBonuses',[]))]:
                for p,c,t in self.flatten(ps,True,label+':'):properties.append(self.property(p,c,t));lines.append(t)
            lines.append('Set members: '+', '.join(self.t(i['Index']) for i in setrow['SetItems']))
        for p in properties:
            if p['property_id']=='sockets' and not p['conditional'] and p['max_value'] is not None:nums['sockets']=p['max_value']
        lines.append('Origin: '+fields['origin'][0])
        fields={k:[v for v in vs if v] for k,vs in fields.items()}
        source_key=str(row.get('Index',code))
        # Keep IDs stable when rows are inserted upstream. Runewords contain one
        # intentional duplicate index (two Doom variants), so its accepted type
        # list forms the stable discriminator. Set name protects against a future
        # item-name collision across sets without relying on row position.
        if tab=='runewords':
            discriminator=','.join(sorted(t.get('Index','') for t in row.get('Types',[]) if isinstance(t,dict)))
            record_id=f'{tab}:{source_key}:{discriminator}'
        elif tab=='sets':record_id=f'{tab}:{row.get("SetName","")}:{source_key}'
        else:record_id=f'{tab}:{source_key}'
        source_file={'uniques':'uniques.json','sets':'sets.json','runewords':'runewords.json'}.get(tab)
        source_row=index
        if tab=='bases':
            is_armor=isinstance(index,int) and index<len(self.armor_rows)
            source_file='armors.json' if is_armor else 'weapons.json'
            source_row=index if is_armor else index-len(self.armor_rows)
        return {'id':record_id,'tab':tab,'name':name,'fields':fields,'numbers':nums,'properties':properties,'display_lines':lines,
                'source_ref':{'file':source_file,'row':source_row,'index':source_key,'code':code},'search_text':'\n'.join(lines)}
    def generate(self):
        rows=[]
        for i,r in enumerate(self.read('keyed/uniques.json')):
            if r.get('Enabled',True):rows.append(self.record(r,'uniques',i))
        for i,s in enumerate(self.read('keyed/sets.json')):
            for j,r in enumerate(s['SetItems']):rows.append(self.record(r,'sets',f'{i}-{j}',s))
        for i,r in enumerate(self.read('keyed/runewords.json')):
            if r.get('Enabled',True):rows.append(self.record(r,'runewords',i))
        for i,r in enumerate(self.base_rows):rows.append(self.record(r,'bases',i))
        return rows

def validate(db):
    if not isinstance(db,dict) or db.get('schema_version')!=1:raise ValueError('unsupported or missing schema_version')
    if not isinstance(db.get('provenance'),dict):raise ValueError('missing provenance')
    if not isinstance(db.get('records'),list) or not db['records']:raise ValueError('records must be a non-empty array')
    if len(db['records'])>100000:raise ValueError('record count exceeds schema maximum')
    ids=set();counts=collections.Counter()
    required={'id','tab','name','fields','numbers','properties','display_lines','source_ref','search_text'}
    for offset,r in enumerate(db['records']):
        if not isinstance(r,dict) or not required.issubset(r):raise ValueError(f'record {offset} is missing required fields')
        if not isinstance(r['id'],str) or not r['id'] or r['id'] in ids:raise ValueError(f'invalid or duplicate id at record {offset}')
        ids.add(r['id'])
        if r['tab'] not in ('uniques','sets','runewords','bases'):raise ValueError(f'invalid tab for {r["id"]}')
        if not isinstance(r['name'],str) or not r['name']:raise ValueError(f'missing name for {r["id"]}')
        counts[r['tab']]+=1
        if not isinstance(r['fields'],dict) or any(not isinstance(v,list) or any(not isinstance(x,str) for x in v) for v in r['fields'].values()):raise ValueError(f'invalid fields for {r["id"]}')
        if not isinstance(r['numbers'],dict) or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in r['numbers'].values()):raise ValueError(f'invalid numbers for {r["id"]}')
        if not isinstance(r['display_lines'],list) or not r['display_lines'] or any(not isinstance(x,str) for x in r['display_lines']):raise ValueError(f'invalid display lines for {r["id"]}')
        if not isinstance(r['search_text'],str):raise ValueError(f'invalid search text for {r["id"]}')
        source=r['source_ref']
        if not isinstance(source,dict) or source.get('file') not in ('uniques.json','sets.json','runewords.json','armors.json','weapons.json') or not isinstance(source.get('row'),(int,str)):raise ValueError(f'invalid source reference for {r["id"]}')
        if not isinstance(r['properties'],list):raise ValueError(f'invalid properties for {r["id"]}')
        for p in r['properties']:
            if not isinstance(p,dict) or not isinstance(p.get('property_id'),str) or not p['property_id']:raise ValueError(f'invalid property for {r["id"]}')
            lo,hi=p.get('min_value'),p.get('max_value')
            if (lo is None)!=(hi is None):raise ValueError(f'incomplete property range for {r["id"]}')
            if lo is not None and (isinstance(lo,bool) or isinstance(hi,bool) or not isinstance(lo,(int,float)) or not isinstance(hi,(int,float)) or not math.isfinite(lo) or not math.isfinite(hi) or lo>hi):raise ValueError(f'invalid property range for {r["id"]}')
            if not isinstance(p.get('conditional'),bool) or not isinstance(p.get('per_level'),bool):raise ValueError(f'invalid property flags for {r["id"]}')
    if set(counts)!=set(('uniques','sets','runewords','bases')):raise ValueError(f'missing catalog tabs: {counts}')
    return dict(counts)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=pathlib.Path,help='website root or static/data directory')
    ap.add_argument('--revision',help='Explicit website commit, defaults to upstream.lock.json')
    ap.add_argument('--output',type=pathlib.Path,default=ROOT/'data/database.json');args=ap.parse_args()
    revision=args.revision or json.loads((ROOT/'upstream.lock.json').read_text())['website']['sha']
    source=args.source or ROOT/'.deps/website-data'/revision
    if args.source and (source/'static/data').is_dir():source=source/'static/data'
    hashes={}
    for f in FILES:
        dest=source/f
        if not args.source and not dest.exists():
            url=f'https://raw.githubusercontent.com/D2R-Reimagined/d2r-reimagined-website/{revision}/static/data/{f}'
            data=urllib.request.urlopen(url,timeout=60).read();dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        hashes[f]=hashlib.sha256(dest.read_bytes()).hexdigest()
    n=Normalizer(source);records=n.generate()
    source_counts={
        'uniques':sum(r.get('Enabled',True) for r in n.read('keyed/uniques.json')),
        'sets':sum(len(s.get('SetItems',[])) for s in n.read('keyed/sets.json')),
        'runewords':sum(r.get('Enabled',True) for r in n.read('keyed/runewords.json')),
        'bases':len(n.base_rows),
    }
    db={'schema_version':1,'provenance':{'repository':'D2R-Reimagined/d2r-reimagined-website','revision':revision,'source_hashes':hashes,'language':'enUS','warnings':dict(n.warnings)},'records':records}
    counts=validate(db);args.output.parent.mkdir(parents=True,exist_ok=True)
    if counts!=source_counts:raise ValueError(f'Normalized counts {counts} do not match source counts {source_counts}')
    args.output.write_text(json.dumps(db,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    (args.output.parent/'property-catalog.json').write_text(json.dumps(sorted(n.catalog.values(),key=lambda x:x['id']),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    property_count=sum(len(r['properties']) for r in records)
    numeric_property_count=sum(p['min_value'] is not None for r in records for p in r['properties'])
    conditional_property_count=sum(p['conditional'] for r in records for p in r['properties'])
    (args.output.parent/'generation-report.json').write_text(json.dumps({
        'counts':counts,'source_counts':source_counts,'properties':property_count,
        'numeric_properties':numeric_property_count,'conditional_properties':conditional_property_count,
        'warnings':dict(n.warnings),'sha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),
        'bytes':args.output.stat().st_size},indent=2)+'\n')
    print(json.dumps({'counts':counts,'warnings':dict(n.warnings),'bytes':args.output.stat().st_size}))
if __name__=='__main__':main()
