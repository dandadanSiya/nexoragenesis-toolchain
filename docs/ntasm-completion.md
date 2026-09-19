# B2.1 — livraison de NTASM complet

Objectif utilisateur actif : finir NTASM et l'améliorer pour résoudre les
difficultés rencontrées avec NASM. Le bootstrap partiel n'est pas l'achèvement.

Décision utilisateur du 4 septembre 2026 : **B2.1 livre NTASM v0 complet**.
Statut actuel : **NON LIVRÉ**. Ce document remplace l'ordre « NTASM en
dernier » de l'ancien plan directeur. NTASM complet n'attend plus la livraison
de NX, Nova GPU, du runtime IA ou de l'OS.

Décision du 5 septembre : regrouper le chantier en une grosse mise à jour.
Le source ASM actif unique est
`bootstrap/quarantine/nasm/ntasm-bootstrap.asm`, édité en place.
Les anciens `seed-b1.*.asm` restent historiques; aucune nouvelle copie
versionnée n'est créée à chaque étape. Les tracers servent de tests internes,
pas de prétexte à multiplier les livraisons partielles.

## Ordre de livraison

Révision demandée par l'utilisateur : développer Nova Bootstrap minimal avec
NTASM-S0 avant de finir v0, afin d'écrire la suite de NTASM en Nova. Cette
étape intermédiaire remplace l'ordre « NTASM complet avant tout Nova », sans
retirer aucun critère A1–A10. Le Nova GPU complet reste hors de cette dépendance.

1. **B1 : assembleur de bootstrap S0 complet.** Les tracers B1.x construisent
   lexer, symboles, encodeurs, relocations et writer.
2. **B2 : auto-hébergement du bootstrap.** Reconstructions et point fixe.
3. **B2.1 : NTASM v0 complet, utilisable et vérifié.** Fonctions, types,
   contrats, macros/comptime, stubs et sorties prévues effectivement livrés.
4. Les autres frontends et produits Genesis réutilisent le cœur et le backend
   commun; leur livraison ne bloque pas celle de NTASM.

Le chemin NTASM-S0 → sources NTASM → NTASM v0 doit être exécutable avec nos
outils sans attendre un compilateur Cnuva/NX/Nova complet. Les composants
communs nécessaires sont construits avec les outils maison disponibles;
cette réorganisation ne crée pas un second backend CPU.

## Acceptation obligatoire de B2.1

| ID | Livrable | Preuve exigée |
|---|---|---|
| A1 | Grammaire et formats v0 figés | Tous les prérequis de spec/ntasm.md §17 sont définis; aucune fonction prévue remplacée par un placeholder |
| A2 | Assembleur pour la première cible x86_64 | Catalogue v0 au-delà des 30 mnémoniques du seul S0, encodages, adressages, symboles et relocations testés |
| A3 | Modules, fonctions et typage machine | Largeurs, registres, features CPU, espaces mémoire et interfaces vérifiés sur programmes complets |
| A4 | effects/clobbers/requires/ensures et ABI | Cas invalides refusés avec positions; appels et chemins interruption/syscall conformes |
| A5 | Macros AST et comptime | Hygiène, provenance, absence d'I/O et bornes imposées, avec tests adverses |
| A6 | Stubs système structurés | Génération depuis des déclarations; chemins concernés validés sous QEMU |
| A7 | Sorties et chaîne commune | NTIR/Machine IR et NXO/PE versionnés; programmes assemblés, chargés et exécutés, inspecteurs et goldens |
| A8 | Auto-hébergement et retrait de NASM | Point fixe du producteur final, artefacts requis reconstruits, aucun NASM ni entrée de quarantaine dans le build normal |
| A9 | Outil utilisable | Commande d'assemblage documentée, sources, binaire, exemples complets et guide; parcours source → exécutable reproductible |
| A10 | Améliorations démontrées | Diagnostics de largeur/registre/section/ABI, limites macros et layout déterministe vérifiés sur cas concrets |

Chaque ID doit pointer vers une preuve réelle du manifeste de livraison.
Une preuve limitée à un parseur, un préfixe ou un probe ne valide pas un
livrable complet. Si un ID manque, B2.1 reste non livré. Les commentaires
et exemples pédagogiques restent une partie du projet.

## Résultats à prouver

| Résultat | Autorité / preuve attendue | État |
|---|---|---|
| Assembleur S0 complet | spec/ntasm-s0.md; B1.1–B1.26, goldens des 30 mnémoniques et PE réellement écrit puis chargé | Producteur deux passes/PE opérationnel, trois sorties exécutées; conformité exhaustive et promotion B1 encore à achever |
| Auto-hébergement | B2, sources NTASM du producteur et point fixe byte-identique sans invocation NASM | Point fixe de la chaîne S0 courante observé pour hôte+cœur (out/ntasm/native-selfhost-checkpoint.md); promotion B2 encore conditionnée par la conformité S0 |
| NTASM typé | spec/ntasm.md, grammaire/catalogues/ABI/IR/macros/stubs figés puis tests positifs et négatifs | Parseur natif v0 en développement séparé; reconnaissance syntaxique ne vaut pas typage, contrats ou encodage complet |
| Améliorations vérifiées | diagnostics de largeur/registre/section/ABI; macros AST bornées et hygiéniques; layout/relocations déterministes | Contrôles de sections et positions partiels |
| Retrait du producteur étranger | Build normal refusant NASM; provenance et sorties inspectables | Lanceur NTBUILD natif pour paire S0 figée, recettes/pins/SHA et refus testés (out/ntasm/agent-release-native-next/checkpoint.md); adaptation au producteur v0 final non achevée |

Les défauts rencontrés localement concernent notamment les offsets PE recopiés
manuellement, les ambiguïtés lexicales et les erreurs de contrats mémoire.
Cela ne prouve pas des bugs intrinsèques de NASM. Les améliorations NTASM doivent
être démontrées sur des exemples reproductibles.

Le profil S0 reste fermé. Les fonctionnalités typées sont développées dans le
profil final après gel de ses contrats; elles ne sont pas ajoutées implicitement
au bootstrap. L'objectif reste actif tant qu'un résultat ci-dessus manque.

## Reprise vérifiée

B1.9 et B1.10 sont promus au 5 septembre 2026. B1.9 passe 43 cas nouveaux
et 146 régressions; B1.10 passe 12 cas nouveaux et 189 régressions.
Les reprises excluent explicitement les anciens arrêts QEMU forcés.
Le runner r3 attend la fermeture distante après quit puis exige exit 0.
Les sources du bootstrap centralisent désormais le layout PE; trois builds
B1.10 sont identiques. Preuves sous `out/b1/step-9/promotion-r1/` et
`out/b1/step-10/promotion-r1/`.

Reprise nocturne du 5 septembre : le source unique couvre maintenant données,
registres/immédiats, mémoire/SIB, branches, RIP et références addr. Les preuves
et hashes courants sont dans MEM.md et out/ntasm/producer-checkpoint.md.
Le producteur normal écrit maintenant un PE réellement chargé et exécuté :
hello, seconde source à layout différent et preuve DIR64 hors base préférée.
Les modes *_SNAPSHOT/VALIDATE_ONLY restent des tests, pas le produit normal.
Prochaine étape : finir la conformité S0, auto-hébergement puis v0 typé selon
A1–A10. Le seul producteur S0 ne valide pas B2.1, qui reste non livré.
