#!/usr/bin/env python3
"""Score a blind second annotator's labels against the Gemini judge.

Joins the second annotator's labels (uid -> category, reachability_related) to the audit sample (which carries
the Gemini judge's labels) and reports inter-annotator agreement + Cohen's kappa on: the 6-way
primary category, the reachability_related flag, the narrow tier (category == 1), and the broad
tier (category == 1 OR reachability_related -- the upper bound of the reachability share). Also prints the
category confusion so disagreements are visible.

Usage: python exp_reach/score_audit.py <audit_sample.json> <second_annotator_labels.json>
       (labels: [{"uid", "category", "reachability_related"}]; writes audit_agreement.json next to the sample)
"""
import collections
import json
import os
import sys


def kappa(pairs):
    """Cohen's kappa for a list of (a, b) categorical labels."""
    n = len(pairs)
    if not n:
        return None
    po = sum(a == b for a, b in pairs) / n
    ca = collections.Counter(a for a, _ in pairs)
    cb = collections.Counter(b for _, b in pairs)
    pe = sum((ca[k] / n) * (cb[k] / n) for k in set(ca) | set(cb))
    return round((po - pe) / (1 - pe), 3) if pe != 1 else 1.0


def main():
    if len(sys.argv) != 3 or sys.argv[1] in ("-h", "--help"):
        sys.exit(__doc__)
    sample = {s["uid"]: s for s in json.load(open(sys.argv[1]))}
    opus = {o["uid"]: o for o in json.load(open(sys.argv[2]))}
    uids = [u for u in sample if u in opus]
    missing = [u for u in sample if u not in opus]

    cat_pairs, reach_pairs, narrow_pairs, broad_pairs = [], [], [], []
    confusion = collections.Counter()
    for u in uids:
        g, o = sample[u], opus[u]
        gc, oc = int(g["gemini_cat"]), int(o["category"])
        gr, orr = bool(g["gemini_reach"]), bool(o["reachability_related"])
        cat_pairs.append((gc, oc))
        reach_pairs.append((gr, orr))
        narrow_pairs.append((gc == 1, oc == 1))
        broad_pairs.append((gc == 1 or gr, oc == 1 or orr))
        if gc != oc:
            confusion[(gc, oc)] += 1

    def agree(pairs):
        return round(sum(a == b for a, b in pairs) / len(pairs), 3)

    print(f"scored {len(uids)}/{len(sample)} turns (annotator missing {len(missing)})")
    print(f"  6-way category   : agreement {agree(cat_pairs)*100:.1f}%  kappa {kappa(cat_pairs)}")
    print(f"  reachability_rel : agreement {agree(reach_pairs)*100:.1f}%  kappa {kappa(reach_pairs)}")
    print(f"  NARROW (cat==1)  : agreement {agree(narrow_pairs)*100:.1f}%  kappa {kappa(narrow_pairs)}")
    print(f"  BROAD (1 or rel) : agreement {agree(broad_pairs)*100:.1f}%  kappa {kappa(broad_pairs)}")
    # how each side calls the tiers (base rates)
    print(f"  base rates narrow: judge {sum(a for a,_ in narrow_pairs)}/{len(uids)}  "
          f"annotator {sum(b for _,b in narrow_pairs)}/{len(uids)}")
    print(f"  base rates broad : judge {sum(a for a,_ in broad_pairs)}/{len(uids)}  "
          f"annotator {sum(b for _,b in broad_pairs)}/{len(uids)}")
    if confusion:
        print("  top category disagreements (judge->annotator): " +
              ", ".join(f"{g}->{o}:{n}" for (g, o), n in confusion.most_common(8)))
    out = {"n": len(uids), "missing": missing,
           "category_agreement": agree(cat_pairs), "category_kappa": kappa(cat_pairs),
           "reach_agreement": agree(reach_pairs), "reach_kappa": kappa(reach_pairs),
           "narrow_agreement": agree(narrow_pairs), "narrow_kappa": kappa(narrow_pairs),
           "broad_agreement": agree(broad_pairs), "broad_kappa": kappa(broad_pairs),
           "confusion": {f"{g}->{o}": n for (g, o), n in confusion.items()}}
    path = os.path.join(os.path.dirname(os.path.abspath(sys.argv[1])), "audit_agreement.json")
    json.dump(out, open(path, "w"), indent=1)
    print(f"-> {path}")


if __name__ == "__main__":
    main()
