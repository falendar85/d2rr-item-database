"""Validate generated artifacts using only the Python standard library."""
import argparse, hashlib, json, pathlib
from generate_database import validate

ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--database',type=pathlib.Path,default=ROOT/'data/database.json')
    parser.add_argument('--report',type=pathlib.Path,default=ROOT/'data/generation-report.json')
    parser.add_argument('--properties',type=pathlib.Path,default=ROOT/'data/property-catalog.json')
    args=parser.parse_args()
    database=json.loads(args.database.read_text(encoding='utf-8'))
    report=json.loads(args.report.read_text(encoding='utf-8'))
    catalog=json.loads(args.properties.read_text(encoding='utf-8'))
    counts=validate(database)
    if counts!=report['counts'] or counts!=report['source_counts']:
        raise ValueError(f'count mismatch: database={counts}, report={report}')
    digest=hashlib.sha256(args.database.read_bytes()).hexdigest()
    if digest!=report['sha256']:raise ValueError('database SHA-256 does not match generation report')
    ids=[entry['id'] for entry in catalog]
    if ids!=sorted(ids) or len(ids)!=len(set(ids)):raise ValueError('property catalog is not sorted and unique')
    known=set(ids)
    missing=sorted({p['property_id'] for r in database['records'] for p in r['properties']}-known)
    if missing:raise ValueError('properties missing from catalog: '+', '.join(missing))
    print(json.dumps({'valid':True,'counts':counts,'properties':report['properties'],
                      'sha256':digest,'warnings':database['provenance']['warnings']}))

if __name__=='__main__':main()
