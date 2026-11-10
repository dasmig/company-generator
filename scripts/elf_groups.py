"""ISO 20275 forms in the four groups of the design (section 5.4), by their names.

The rules read the local and transliterated names and the abbreviations; curated/legal_forms.tsv
overrides any form the rules put in the wrong group. Order matters: the first rule that matches
wins, so the narrow ones (funds, public bodies, co-operatives) come before the broad ones.
"""
import csv
import re
from collections import defaultdict

from companygen_data import CURATED, RAW, read_tsv

RULES = [
    ("fund", r"\bfund\b|\bfonds?\b|fondo d|investment compan|unit trust|\bsicav\b|\bsicaf\b|\bucits\b|"
             r"\bfcp\b|fundo de investimento|fondo de inversi|anlagefonds|sondervermögen|investmentfonds|"
             r"investmentvermögen|beleggingsfonds|\bfcpe\b|organisme de placement|\bopcvm\b|\bfia\b|\bsicv\b"),
    ("other", r"corporation sole|товарищества собственников|некоммерческ|учрежден|кооператив|общественн|"
              r"профсоюз|унитарн|казенн|политическ|government|ministr|unlimited|state agenc|municipal|county|\bcity\b|public (?:body|authority|institution|law)|"
              r"öffentlich|des öffentlichen rechts|établissement public|collectivité|entidad pública|"
              r"organismo público|autarqu|\bkommun|\bstatlig|\bstatens\b|valtion|kunta|public corporation|"
              r"statutory|parastatal|central bank|church|kirche|église|iglesia|religio|"
              r"\bverein\b|association|asociaci|associa[çc]|\bförening|\bforening|yhdistys|stowarzyszenie|"
              r"vereniging|\bvzw\b|\basbl\b|foundation|\bstiftung|fondation|fundaci|funda[çc]|stichting|\bstiftelse|"
              r"säätiö|fundacja|\btrust\b|charit|non-?profit|not for profit|sans but lucratif|gemeinnützig|"
              r"co-?operative|cooperativ|genossenschaft|coopérative|coöperatie|\bandelslag|\bsamvirkelag|"
              r"osuuskunta|spółdzielnia|kooperativ|mutual|mutuelle|mutua\b|versicherungsverein|friendly society|"
              r"\bunion\b|syndicat|sindicato|gewerkschaft|political party|partido|parti politique|"
              r"branch|zweigniederlassung|succursale|sucursal|filial|niederlassung|representative office|"
              r"\bnuf\b|enkeltpersonforetak med|economic interest grouping|\bgie\b|\bewiv\b|\bgeie\b|\beeig\b|"
              r"fondazione|\bfonde\b|selvejende|privatstiftung|\bente\b|ecclesiastic|congregaci|congrégation|"
              r"kerkgenootschap|trossamfund|wspólnoty samorządowe|\bobec\b|družstvo|alapítvány|egyesület|"
              r"fideicomiso|treuhänder|del estado|derecho público|registered society|associazione|"
              r"律师|事业单位|机关|社会团体|organismo|alte forme"),
    ("sole_proprietorship", r"крестьянск|фермерск|sole (?:proprietor|trader)|individual (?:entrepreneur|enterprise|business|proprietor)|"
                            r"\bsole\b|einzelunternehm|einzelkaufm|eingetragener kaufmann|\be\.?\s?k\.?\b|"
                            r"entrepreneur individuel|entreprise individuelle|micro-?entrepreneur|"
                            r"empresário individual|empresario individual|empresa individual|"
                            r"microempreendedor|enkeltpersonforetak|enskild (?:näringsidkare|firma)|"
                            r"enkeltmandsvirksomhed|yksityinen elinkeinonharjoittaja|toiminimi|"
                            r"jednoosobow|osoba fizyczna|fyzická osoba|podnikatel|egyéni vállalkoz|"
                            r"eenmanszaak|ditta individuale|imprenditore individuale|empresa unipessoal|"
                            r"индивидуальн|фізична особа|şahıs|ferdi|個人事業|个体|자영|"
                            r"\benk\b|\bei\b|\beirl\b|\bmei\b|impresa individuale|działalność gospodarcz|"
                            r"osoby fizyczne|einzelfirma|egyéni cég"),
    ("partnership", r"partnership|partnerschaft|\bpartg\b|gesellschaft bürgerlichen|\bgbr\b|offene handelsgesellschaft|"
                    r"\bohg\b|kommanditgesellschaft|\bkg\b|en nom collectif|\bsnc\b|en commandite simple|\bscs\b|"
                    r"société civile|sociedad colectiva|sociedad (?:en )?comandita|sociedade em nome coletivo|"
                    r"comandita|in nome collettivo|in accomandita semplice|\bsas\b(?=.*accomandita)|"
                    r"handelsbolag|kommanditbolag|ansvarlig selskap|kommandittselskap|\bans\b|\bda\b|\bks\b|"
                    r"interessentskab|kommanditselskab|avoin yhtiö|kommandiittiyhtiö|spółka jawna|"
                    r"spółka komandytowa|spółka partnerska|vennootschap onder firma|commanditaire vennootschap|"
                    r"maatschap|veřejná obchodní|komanditní|közkereseti|betéti társaság|\bllp\b|\blp\b|"
                    r"полное товарищество|товарищ[а-я]* на вере|хозяйственные товарищества|простые товарищества|"
                    r"nombre colectivo|合名会社|合資会社|合伙|società semplice|offene gesellschaft|"
                    r"\bog\b|commandite|comunidad de bienes|sociedad civil|sociedade simples|sociedade civil|"
                    r"Ομόρρυθμη|Ετερόρρυθμη|mažosios bendrij|komandit|\bkd\b|\bk\.d\.\b|\bd\.n\.o\.|"
                    r"javno trgovačko|družba z neomejeno|събирателно|командитно|شركات\s+اشخاص|تضامن|توصية|"
                    r"\bgaec\b|groupement agricole|\bsecs\b|\bsecsp\b|\bs\.e\.c\.|\bcv\b|\bvof\b"),
    ("limited_liability", r"limited|\bltd\b|beschränkt|\bgmbh\b|\bag\b|aktiengesellschaft|aktiebolag|aksjeselskap|"
                          r"allmennaksjeselskap|anpartsselskab|aktieselskab|osakeyhtiö|responsabilité limitée|"
                          r"responsa?i?bilidad limitada|responsabilidade limitada|responsabilità limitata|"
                          r"par actions|anonyme|anónima|anônima|anonima|per azioni|sociedade por quotas|"
                          r"z ograniczoną|akcyjna|s ručením omezeným|akciová|korlátolt felelősségű|részvénytársaság|"
                          r"besloten vennootschap|naamloze vennootschap|\bbv\b|\bnv\b|corporation|incorporated|"
                          r"\binc\b|\bcorp\b|company|\bllc\b|\bplc\b|kabushiki|godo kaisha|株式会社|合同会社|有限会社|"
                          r"有限公司|有限责任|股份|주식회사|유한회사|общество с ограниченной|акционерное общество|"
                          r"товариство з обмеженою|акціонерне|şirketi|limited liability|\bsarl\b|\bsas\b|\bsa\b|"
                          r"\bsrl\b|\bspa\b|\bsl\b|\bltda\b|\bpt\b|perseroan|\bsdn bhd\b|\bpte\b|\bpty\b|"
                          r"\bjsc\b|\bllc\b|\booo\b|\bzao\b|\bpao\b|\bao\b|\bkft\b|\bzrt\b|\bnyrt\b|\bsro\b|"
                          r"\bas\b|\basa\b|\bab\b|\boy\b|\boyj\b|\baps\b|\bbhd\b|\btbk\b|"
                          r"unipersonal|unipessoal|simplificada|simplifiée|por acciones|por quotas|"
                          r"osaühing|aktsiaselts|\boü\b|akcinė|uždarosios|\buab\b|"
                          r"ierobežotu|akciju sabiedrība|\bsia\b|omejeno odgovornost|delniška|\bd\.o\.o|\bd\.d\.|"
                          r"ograničenom odgovornošću|dioničko|ručením obmedzeným|spoločnosť na akcie|spol\. s r\. o|"
                          r"ограничена отговорност|акционерно дружество|\bеоод\b|\bоод\b|\bеад\b|\bад\b|"
                          r"с ограниченной|ограниченной ответственност|акционерн|"
                          r"Εταιρ[ιίε]+α Περιορισμένης|Ιδιωτική Κεφαλαιουχική|Ιδιωτική Εταιρεία|Ανώνυμη|\bike\b|\bepe\b|"
                          r"anonim şirket|limited şirket|\ba\.ş\.|einkahlutafélag|hlutafélag|\behf\b|\bhf\b|"
                          r"iværksætterselskab|\bivs\b|exercice libéral|\bselas\b|\bselarl\b|"
                          r"ذات مسؤولية محدودة|ذ\.م\.م|مساهمة|บริษัทจำกัด|บริษัทมหาชน|בע\"מ|חברה|"
                          r"企业|公司|会社|회사|national bank|\bbank\b|anstalt|sociedad agrar|\bs\.a\.t\.|"
                          r"evropská společnost|european company|societas europaea|\bse\b|"
                          r"\bs\.?a\.?s\b|\bs\.?r\.?l\b|\bs\.?l\.?u?\b|\bs\.?a\.?\b|\bs\.?l\.?p\b"),
]
COMPILED = [(g, re.compile(rx, re.IGNORECASE)) for g, rx in RULES]


def group_of(texts):
    """The group of a form from its names and abbreviations; None when no rule matches."""
    text = " | ".join(t for t in texts if t)
    for g, rx in COMPILED:
        if rx.search(text):
            return g
    return None


def read_elf():
    """Active ISO 20275 forms: {code: {...}} with every language's names merged."""
    out = {}
    with open(RAW / "gleif" / "elf-code-list-v1.6.csv", encoding="utf-8-sig", newline="") as f:
        for r in csv.DictReader(f):
            if not r["ELF Status ACTV/INAC"].startswith("ACTV") or not r["Country Code (ISO 3166-1)"]:
                continue
            code = r["ELF Code"]
            e = out.setdefault(code, {"code": code, "country": r["Country Code (ISO 3166-1)"],
                                      "subdivision": r["Country sub-division code (ISO 3166-2)"],
                                      "names": [], "abbreviations": [], "latin_names": [], "latin_abbreviations": [],
                                      "languages": [], "abbr_by_language": [], "latin_abbr_by_language": [],
                                      "created": r["Date created YYYY-MM-DD (ISO 8601)"]})
            e["names"].append(r["Entity Legal Form name Local name"].strip())
            e["latin_names"].append(r["Entity Legal Form name Transliterated name (per ISO 01-140-10)"].strip())
            e["abbreviations"] += [a.strip() for a in r["Abbreviations Local language"].split(";") if a.strip()]
            e["latin_abbreviations"] += [a.strip() for a in r["Abbreviations transliterated"].split(";") if a.strip()]
            e["languages"].append(r["Language Code (ISO 639-1)"])
            e["abbr_by_language"].append([a.strip() for a in r["Abbreviations Local language"].split(";") if a.strip()])
            e["latin_abbr_by_language"].append([a.strip() for a in r["Abbreviations transliterated"].split(";")
                                                if a.strip()])
    for e in out.values():
        e["group"] = (group_of(e["names"] + e["latin_names"]) or
                      group_of(e["abbreviations"] + e["latin_abbreviations"]))
    for r in read_tsv(CURATED / "elf_overrides.tsv"):
        e = out.get(r["code"])
        if e is None or e["country"] != r["country"]:
            raise ValueError(f"elf_overrides.tsv: {r['code']} is not an active {r['country']} form")
        e["group"] = r["group"]
    return out


def by_country(elf):
    d = defaultdict(list)
    for e in elf.values():
        d[e["country"]].append(e)
    return d
