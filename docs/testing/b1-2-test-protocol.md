# Protocole de test — tracer B1.2 enveloppe physique

## But, statut et limite de preuve

Ce protocole a figé **avant implémentation** la matrice RED → GREEN de B1.2. Il
couvre la lecture sentinelle de `\NTASM1.NTASM`, les octets physiques autorisés,
les EOL et les limites de taille, de longueur de ligne et de nombre de lignes.

Statut au 2 septembre 2026 : **tracer B1.2 GREEN, Gate B1 RED**. Le candidat
vérifié est le PE externe quarantainé produit depuis
`bootstrap/quarantine/nasm/seed-b1.2-r1.asm`. Ce GREEN ne prouve ni le lexer
complet, ni les deux passes, ni le writer PE32+, ni la Gate B1.

Autorités : `spec/ntasm-s0.md`, `docs/bootstrap/b1-implementation-map.md`, le
protocole B1.1 `docs/testing/b1-test-protocol.md`, le manifeste byte-exact
`bootstrap/ntasm-s0/b1-2-fixtures.md` et l'exception de provenance ADR 0009.

## Contrat observable

Le nouveau succès temporaire est exactement `NG_B1_2_PHYSICAL_OK`, suivi de
CRLF. Il est distinct de `NG_B1_1_FORMAT_OK`, `NG_SEED0_OK` et du succès final
interdit à ce stade, `NG_NTASM_S0_OK`.

Le scanner physique s'exécute avant la reconnaissance B1.1, mais ne la remplace
pas. Le marqueur B1.2 apparaît seulement si l'enveloppe physique entière est
valide **et** si la première ligne sémantique reste exactement `format pe64`.
Ainsi, une source d'un seul octet LF est physiquement valide mais produit E200,
pas le marqueur. `bad-format.ntasm` reste E200 après B1.2.

Une source acceptée produit exactement une ligne `NG_B1_2_PHYSICAL_OK`, aucun
diagnostic, aucun autre marqueur, puis `EFI_SUCCESS`. Une source refusée produit
exactement un diagnostic `NGS0 Eddd Lhhhh Chhhh Ohhhhhhhh`, aucun marqueur, puis
`EFI_LOAD_ERROR`. Ligne et colonne sont hexadécimales 1-based, offset byte
hexadécimal 0-based. Une erreur sans position exploitable utilise des zéros.

## Matrice RED/GREEN B1.2 figée

Les tailles, recettes et SHA-256 sont normatifs dans le manifeste des fixtures.

| ID | Cas | Taille | Oracle GREEN exact |
|---|---|---:|---|
| B12-00 | source vide — régression B1.1 déjà GREEN, pas un nouveau RED | 0 | `NGS0 E100 L0000 C0000 O00000000` |
| B12-01 | golden minimal valide | 169 | `NG_B1_2_PHYSICAL_OK` |
| B12-02 | source maximale acceptée | 65 535 | `NG_B1_2_PHYSICAL_OK` |
| B12-03 | sentinelle trop grande | 65 536 | `NGS0 E500 L0000 C0000 O00000000` |
| B12-04 | NUL | 14 | `NGS0 E100 L0002 C0001 O0000000C` |
| B12-05 | BOM UTF-8 | 15 | `NGS0 E100 L0001 C0001 O00000000` |
| B12-06 | non-ASCII | 14 | `NGS0 E100 L0002 C0001 O0000000C` |
| B12-07 | CR isolé | 15 | `NGS0 E100 L0002 C0001 O0000000C` |
| B12-08 | ligne de 255 octets hors EOL | 268 | `NG_B1_2_PHYSICAL_OK` |
| B12-09 | ligne de 256 octets hors EOL | 269 | `NGS0 E101 L0002 C0100 O0000010B` |
| B12-10 | 4 096 lignes | 4 107 | `NG_B1_2_PHYSICAL_OK` |
| B12-11 | 4 097 lignes | 4 108 | `NGS0 E500 L1001 C0001 O0000100B` |
| B12-12 | dernière ligne sans EOL | 11 | `NGS0 E100 L0001 C000C O0000000B` |
| B12-13 | CR terminal après `format pe64` LF | 13 | `NGS0 E100 L0002 C0001 O0000000C` |
| B12-14 | BOM tronqué à `EF` | 1 | `NGS0 E100 L0001 C0001 O00000000` |
| B12-15 | BOM tronqué à `EF BB` | 2 | `NGS0 E100 L0001 C0001 O00000000` |
| B12-16 | TAB seul après la première ligne | 14 | `NG_B1_2_PHYSICAL_OK` |
| B12-17 | borne ASCII basse `20` | 14 | `NG_B1_2_PHYSICAL_OK` |
| B12-18 | borne ASCII haute `7E` | 14 | `NG_B1_2_PHYSICAL_OK` |
| B12-19 | octet `7F` | 14 | `NGS0 E100 L0002 C0001 O0000000C` |
| B12-20 | octet `0B` | 14 | `NGS0 E100 L0002 C0001 O0000000C` |
| B12-21 | EOL mixtes LF, CRLF, puis LF | 17 | `NG_B1_2_PHYSICAL_OK` |
| B12-22 | ligne de 255 octets suivie de CRLF | 269 | `NG_B1_2_PHYSICAL_OK` |
| B12-23 | source composée du seul LF — régression sémantique B1.1 | 1 | `NGS0 E200 L0001 C0001 O00000000` |

Le 256e octet de B12-09 et le 4 097e EOL de B12-11 sont refusés avant
incrément. Un code correct avec une position différente est un échec. Les cas
positifs B12-02, B12-08 et B12-10 commencent par `format pe64` afin d'atteindre
le marqueur tout en isolant la frontière physique testée.

B12-13 pointe le CR lui-même, car son successeur obligatoire n'existe pas.
B12-14 et B12-15 pointent le premier octet `EF` : un préfixe de BOM tronqué ne
devient ni une source vide ni une erreur située à l'EOF. B12-16 à B12-22
commencent également par la première ligne exacte. B12-23 prouve séparément
qu'une enveloppe physique valide ne contourne pas la reconnaissance B1.1.

## RED obligatoire sur B1.1-r1

Utiliser sans modification
`bootstrap/seed/b1/seed-b1.1-r1.efi`, SHA-256
`86d43044d32f000a89b1d66ccd134c7ba9308d59f434d70988ea6a8ef1d816b7`.

Rejouer d'abord B12-00 et B12-23 sur B1.1-r1 comme contrôles hérités : ils sont
déjà GREEN avec respectivement E100 et E200 exacts et ne comptent pas comme de
nouveaux RED. Pour chacun des **22 autres cas** :

1. matérialiser la recette sous le seul nom racine `\NTASM1.NTASM` dans un
   volume EFI jetable ;
2. vérifier taille et SHA-256 avant le boot ;
3. confirmer que `\NTASM1.EFI` est absent ;
4. démarrer avec un magasin de variables OVMF neuf ;
5. capturer la sortie et faire échouer l'assertion GREEN de la matrice ;
6. confirmer le retour au firmware et l'absence de sortie après le boot.

Le RED est valide seulement si le banc fonctionne. B1.1-r1 ne possède jamais le
marqueur B1.2 et son lecteur partiel ne peut pas prouver les erreurs placées
après la première ligne. Conserver pour chaque ID la ligne réellement observée
et la raison de l'écart. Un crash, un seed non chargé ou un volume illisible est
un banc cassé, pas un RED.

Le dossier de preuve RED contient donc deux contrôles hérités GREEN et 22
assertions nouvelles RED. Il est interdit de présenter B12-00 comme une nouvelle
preuve de défaut de B1.1-r1.

## GREEN et lecture sentinelle

Après l'implémentation minimale, rejouer les **24 cas** avec le même binaire
B1.2, un volume et un magasin OVMF neufs par cas. Comparer byte pour byte
l'unique ligne console, vérifier le statut, le retour propre au firmware et
l'inventaire du volume avant/après.

Le lecteur appelle `Read` en boucle avec la capacité restante. Une taille rendue
nulle signifie EOF. Zéro octet au total devient E100. Dès que le cumul atteint
`0x10000`, la source devient E500, sans troncature et sans scanner son contenu.
Un cumul `0xFFFF` n'est accepté qu'après un appel suivant de capacité 1 rendant
zéro.

Le scanner ne charge jamais un octet avant d'avoir prouvé
`cursor < source_len`. Il accepte uniquement `09`, LF, CR suivi de LF et
`20..7E`. CRLF compte comme un EOL. Le couple est lu seulement après avoir
prouvé que le successeur existe. Une ligne de 255 octets hors EOL et la 4 096e
ligne sont valides ; le premier `+1` produit respectivement E101 et E500.

## Non-régression B1.1 obligatoire

Après la matrice physique, rejouer aussi les autorités B1.1 :

| Cas | Résultat B1.2 exigé |
|---|---|
| source absente | `NGS0 E001 L0000 C0000 O00000000` |
| `NTASM1.NTASM` est un répertoire non vide | E002 ; le chemin passe par `GetInfo`/lecture et ne devient jamais E100/E500 |
| `invalid/bad-format.ntasm` | `NGS0 E200 L0001 C0001 O00000000` |
| `golden/minimal-crlf.ntasm` | `NG_B1_2_PHYSICAL_OK` |
| B12-23, source composée du seul octet LF | `NGS0 E200 L0001 C0001 O00000000`, aucun marqueur |

Ces cas prouvent que le scanner complet n'accepte pas silencieusement une
source seulement parce que son enveloppe physique est valide.

## Trois allocations et canaris forts

Il existe exactement trois `AllocatePool(EfiLoaderData)`, dans l'ordre WORK,
SRC, OUT, sans `ReallocatePool`. Chaque allocation contient un payload normatif
encadré par deux gardes de 16 octets ; la taille allouée est donc payload
`+ 0x20`.

| Ordre | Pool | Payload | Taille allouée |
|---:|---|---:|---:|
| 1 | `WORK` | `0x1D000` | `0x1D020` |
| 2 | `SRC` | `0x10000` | `0x10020` |
| 3 | `OUT` | `0x10000` | `0x10020` |

Le pointeur de payload vaut `allocation_base + 0x10`. Les six gardes sont
distinctes et figées byte pour byte :

| Garde | 16 octets exacts |
|---|---|
| `WORK_PRE` | `A5 5A C3 3C 96 69 F0 0F 55 AA 33 CC 78 87 E1 1E` |
| `WORK_POST` | `1E E1 87 78 CC 33 AA 55 0F F0 69 96 3C C3 5A A5` |
| `SRC_PRE` | `99 66 FF 00 AA 55 CC 33 69 96 0F F0 44 BB DD 22` |
| `SRC_POST` | `22 DD BB 44 F0 0F 96 69 33 CC 55 AA 00 FF 66 99` |
| `OUT_PRE` | `66 99 00 FF 55 AA 33 CC 96 69 F0 0F BB 44 22 DD` |
| `OUT_POST` | `DD 22 44 BB 0F F0 69 96 CC 33 AA 55 FF 00 99 66` |

Les six gardes sont comparées byte pour byte après fermeture de la source et
avant diagnostic ou succès. Toute différence est une erreur interne E603, à
position nulle, interdit tout marqueur et toute sortie. Cela couvre notamment
la lecture exacte des 65 536 octets de la sentinelle sans écrire le premier
octet de `SRC_POST`. Le payload OUT reste entièrement inchangé en B1.2.

B1.2 prouve statiquement le chemin E603 par le source NASM quarantainé, le désassemblage, les six
constantes distinctes, les comparaisons et le branchement vers le diagnostic.
Il vérifie aussi à l'exécution que les gardes restent intactes dans tous les cas
normaux. Aucune fixture B1.2 ne corrompt volontairement une garde : l'injection
runtime d'un canari fautif est explicitement reportée à B1.25.

Le contenu source ne change ni nombre ni taille d'allocations. Une allocation
échouée libère seulement ce qui a déjà été acquis. Tous les chemins utilisent un
épilogue unique : fermer `source` si ouvert, fermer `root`, puis libérer OUT,
SRC et WORK en ordre inverse, une fois chacun. Sur diagnostic, la ligne est
formée tant que le scratch WORK existe, puis l'épilogue termine le cleanup. Le
marqueur positif n'est affiché qu'après canaris intacts, handles fermés et trois
libérations réussies. Les injections firmware exhaustives de Close/FreePool
restent hors périmètre et seront traitées au tracer prévu pour les fautes I/O.

## Interdiction absolue de sortie

B1.2 ne sonde, n'ouvre, ne crée, ne tronque, n'écrit et ne supprime jamais
`\NTASM1.EFI`. Cette règle vaut pour succès, diagnostics, E603 et erreurs
d'allocation. Une capture console ne prouve pas l'absence : l'inventaire du
volume avant/après est obligatoire. Tout fichier de ce nom, même vide, invalide
le cas et le tracer entier.

## Exécution vérifiée

La matrice GREEN B12-00 à B12-23 a été exécutée sous QEMU 10.2.0/OVMF avec un
volume et un magasin de variables propres par cas. Les 24 oracles sont exacts,
apparaissent une seule fois, rendent la main au firmware et laissent
`NTASM1.EFI` absent. La vérification mécanique indépendante réside dans
`out/b1/step-2/20260902-172215-65d1b7dcff08475a974763b49f036e46/matrix-verification.txt`.

Les quatre non-régressions obligatoires — source absente, source-répertoire,
`bad-format` et `minimal-crlf` — passent également. Leur campagne indépendante
réside dans `out/b1/step-2/agent-tests-20260902-172705-1f64b0454472/` et sa
revue mécanique dans `nonregression-verification.txt` sous la racine canonique.
L'injection runtime E603 et les fautes E604 restent reportées à B1.25.

## Preuves et promotion du tracer

Le manifeste GREEN canonique doit contenir, pour chaque ID, recette, taille et
hash de la fixture exécutée, source et manifeste NASM quarantainés, hash du candidat, transcript console,
statut/continuation OVMF, absence d'erreur QEMU fatale, inventaires du volume et
revue des trois allocations, six gardes, bornes du scanner et cleanup central.
Le tableau RED original reste à côté du tableau GREEN.

B1.2 est GREEN seulement si les 24 lignes, les non-régressions B1.1,
l'absence de sortie et les canaris/cleanup concordent. L'annonce autorisée est
uniquement « tracer B1.2 GREEN, Gate B1 RED » ; la prochaine étape reste B1.3.
