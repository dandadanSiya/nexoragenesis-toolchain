# NTASM-S0 — profil de bootstrap borné

Statut : spécification normative initiale, version `S0.1`.

Ce document définit le seul langage que `seed0.efi` doit accepter. NTASM-S0
n'est pas encore le NTASM général : c'est un sous-ensemble fermé, déterministe et
assez petit pour écrire `ntasm1.ntasm`, puis produire directement un PE32+
x86_64 UEFI. Le mot **DOIT** exprime une exigence; **INTERDIT** signifie que le
seed doit refuser l'entrée avant de créer le fichier de sortie.

La forme exacte de l'enveloppe produite est le profil canonique `B1-PE-v1`
défini dans [`seed-pe32.md`](seed-pe32.md). Le profil provisoire `B0-PE-v0` n'est
jamais une sortie de l'assembleur. En cas de conflit, ce document décide de la
syntaxe et des encodages d'instructions; `seed-pe32.md` décide du conteneur PE et
de l'ABI UEFI.

## 1. Contrat d'exécution du seed assembleur

Le seed étendu de la Gate B1 est une application UEFI x86_64. Il :

1. ouvre, sur le même volume que l'image chargée, `\NTASM1.NTASM` en lecture;
2. lit au plus `65 536` octets dans un tampon borné, par appels répétés dont la
   capacité est toujours l’espace restant;
3. après `65 535` octets cumulés, effectue un dernier `Read` de capacité `1` :
   un retour de taille zéro confirme EOF, tandis qu’un octet rendu atteint la
   sentinelle `65 536` et refuse la source;
4. refuse une source vide ou une lecture de `65 536` octets, car la taille
   acceptée est `1..65 535` octets;
5. effectue les deux passes complètes en mémoire;
6. construit l'image PE complète en mémoire;
7. n'ouvre `\NTASM1.EFI` qu'après validation et émission complètes;
8. refuse d'écraser `\NTASM1.EFI` s'il existe déjà;
9. écrit exactement la taille calculée, appelle `Flush`, puis `Close`;
10. affiche `NG_NTASM_S0_OK\r\n` par `ConOut.OutputString` et quitte avec
   `EFI_SUCCESS`.

Avant le premier `Read`, le seed appelle `GetInfo(EFI_FILE_INFO_ID)` et refuse
avec E002 tout objet dont `EFI_FILE_INFO.Attribute` porte
`EFI_FILE_DIRECTORY`, ainsi que toute information tronquée ou incohérente.

Les payloads runtime sont `WORK=0x1D000`, `SRC=0x10000` et `OUT=0x10000`.
Chacun est entouré de 16 octets de garde avant et après : les demandes brutes
`AllocatePool(EfiLoaderData)` valent donc `0x1D020`, `0x10020`, `0x10020`, soit
`0x3D060` au total. Le pointeur utilisable est `raw+0x10`; seul `raw` est rendu à
`FreePool`. Les valeurs exactes des six bandes canari ont pour unique autorité
[`b1-2-test-protocol.md`](../docs/testing/b1-2-test-protocol.md); toute
implémentation source ou tout ledger les recopie sans les choisir.

Une erreur de source ne crée jamais `\NTASM1.EFI`. Une erreur d'écriture peut
laisser un nouveau fichier partiel : ce fichier est invalide, le succès n'est pas
affiché et il ne doit jamais être utilisé comme producteur. NTASM-S0 ne lit ni
ligne de commande, ni variable d'environnement, ni réseau, ni chemin fourni par
la source.

## 2. Représentation de la source

- La source est un sous-ensemble ASCII de UTF-8 : octets `0x09`, `0x0A`,
  `0x0D` et `0x20..0x7E` uniquement.
- Les fins de ligne acceptées sont LF (`0A`) et CRLF (`0D 0A`). Un CR isolé est
  invalide.
- Un octet NUL, un BOM, un octet non ASCII ou l'absence de fin de ligne après
  une ligne non vide est invalide.
- La détection du BOM UTF-8 ne compare ses trois octets que si la capacité
  restante est au moins `3`. Avec moins de trois octets restants, aucun
  lookahead n’est effectué; le premier octet non ASCII est traité normalement
  comme E100.
- L'espace et la tabulation séparent les tokens. Ils sont sans effet au début et
  à la fin d'une ligne.
- `;` commence un commentaire jusqu'à la fin de ligne, sauf dans une chaîne.
- Il existe exactement une instruction ou directive par ligne. Le label peut
  occuper une ligne seul; `label: instruction` est interdit.
- Les mots-clés, registres et noms de sections sont en minuscules exactes. Les
  identifiants utilisateur sont sensibles à la casse.

### 2.1 Tokens

| Token | Forme acceptée |
|---|---|
| identifiant | `[A-Za-z_][A-Za-z0-9_]{0,30}` |
| entier non signé | décimal `0` ou `[1-9][0-9]*`; hexadécimal `0x[0-9A-Fa-f]+` |
| entier signé | `-` immédiatement suivi d'un entier non signé; `+` unaire interdit |
| chaîne | `"..."`, sur une seule ligne |
| ponctuation | `: , = [ ] + - * ( )` |
| section | `.text`, `.rdata` ou `.data` |

Les zéros initiaux décimaux, séparateurs `_`, suffixes de taille, caractères et
flottants sont interdits. Un entier est analysé en entier non signé 64 bits puis,
si `-` est présent, converti seulement si sa valeur tient dans `i64`.

Dans une chaîne, seuls les octets ASCII imprimables et les échappements `\\`,
`\"`, `\r`, `\n`, `\t`, `\0` et `\xHH` sont acceptés. `HH` contient exactement
deux chiffres hexadécimaux. Un échappement inconnu est invalide.

## 3. Grammaire normative

L'EBNF ci-dessous décrit les lignes sémantiques après retrait des commentaires et
espaces périphériques. `EOL` est obligatoire après chaque ligne physique.

```text
source       = format-line, abi-line, { const-line }, entry-line,
               text-section, rdata-section, data-section, EOF ;

format-line  = "format", WS, "pe64", EOL ;
abi-line     = "abi", WS, "efi_x64", EOL ;
const-line   = "const", WS, ident, WS?, "=", WS?, signed-integer, EOL ;
entry-line   = "entry", WS, ident, EOL ;

text-section = "section", WS, ".text", WS, "rx", EOL,
               text-statement, { text-statement } ;
rdata-section= "section", WS, ".rdata", WS, "r", EOL,
               data-statement, { data-statement } ;
data-section = "section", WS, ".data", WS, "rw", EOL,
               data-statement, { data-statement } ;

text-statement = empty-line | label-line | align-line | instruction-line ;
data-statement = empty-line | label-line | align-line | scalar-line |
                 string-line | zero-line ;

label-line   = ident, ":", EOL ;
align-line   = "align", WS, ( "2" | "4" | "8" | "16" ), EOL ;
scalar-line  = scalar-kind, WS, scalar, { WS?, ",", WS?, scalar }, EOL ;
scalar-kind  = "byte" | "word" | "dword" | "qword" ;
scalar       = signed-integer | ident | "addr", "(", ident, ")" ;
string-line  = ( "ascii" | "utf16z" ), WS, string, EOL ;
zero-line    = "zero", WS, positive-integer, EOL ;

instruction-line = mnemonic,
                   [ WS, operand, { WS?, ",", WS?, operand } ], EOL ;
operand      = register | signed-integer | ident | memory ;
memory       = "[", WS?, memory-body, WS?, "]" ;
memory-body  = "rip", WS?, "+", WS?, ident
             | register, memory-tail ;
memory-tail  = [ WS?, ( "+" | "-" ), WS?, displacement ]
             | WS?, "+", WS?, index-term,
               [ WS?, ( "+" | "-" ), WS?, displacement ] ;
index-term   = register, WS?, "*", WS?, ( "1" | "2" | "4" | "8" ) ;
displacement = unsigned-integer | ident ;

unsigned-integer = decimal-integer | hexadecimal-integer ;
signed-integer   = [ "-" ], unsigned-integer ;
positive-integer = unsigned-integer autre que zéro ;
```

`WS` signifie un ou plusieurs espaces ou tabulations; `WS?` signifie zéro ou
plus. `empty-line` est une ligne ne contenant que whitespace et/ou commentaire;
`EOF` suit l'EOL obligatoire de la dernière ligne. `register` est un token de la
section 4 et `mnemonic` un nom exact de la section 5. Dans `memory-tail`,
l'alternative indexée est choisie si `*` est présent. Une constante utilisée
comme déplacement doit être non négative et tenir dans `i32`; le signe écrit
devant elle est ensuite appliqué. Une constante négative reste permise dans les
autres positions immédiates et de données.

### 3.1 Ordre et portée

- `format pe64` et `abi efi_x64` sont les deux premières lignes sémantiques.
- Les constantes viennent ensuite, puis l'unique directive `entry`.
- Les sections apparaissent exactement une fois et dans l'ordre `.text`,
  `.rdata`, `.data`; elles doivent toutes être non vides.
- `.reloc` est réservée au writer PE et ne peut pas être nommée dans la source.
- Les constantes sont visibles partout et ne peuvent référencer qu'un entier.
- Un identifiant en position scalaire ou immédiate doit nommer une constante;
  un identifiant de `call`/branche doit nommer un label. Un label ne devient
  jamais implicitement une adresse immédiate : il faut `[rip + label]` ou
  `addr(label)`.
- Les labels sont globaux, définis sur l'offset courant de leur section et
  peuvent être référencés avant leur définition.
- Un même nom ne peut désigner deux symboles, même de catégories différentes.
- La cible de `entry` doit être un label de `.text` situé sur le premier octet
  d'une instruction.
- Les branches et `call label` ciblent seulement `.text`.
- `[rip + label]` peut cibler n'importe laquelle des trois sections.
- `addr(label)` est accepté uniquement dans un élément `qword` naturellement
  aligné sur 8 octets de `.rdata` ou `.data`.
- Il doit exister au moins un `qword addr(label)`. Cette exigence garantit une
  table de relocation PE non vide et chargeable à une base quelconque.

### 3.2 Directives de données

- `byte`, `word`, `dword` et `qword` émettent respectivement 1, 2, 4 et 8 octets
  little-endian. Un entier ou une constante doit tenir dans la largeur sans
  troncature. Une valeur négative doit tenir dans la largeur signée et est émise
  en complément à deux.
- `addr(label)` émet `ImageBase + RVA(label)` sur 64 bits et crée une relocation
  `IMAGE_REL_BASED_DIR64` à l'adresse du qword.
- `ascii` émet les octets décodés sans terminaison.
- `utf16z` exige que chaque octet décodé soit inférieur ou égal à `0x7F`, émet
  chaque valeur comme un code unit UTF-16LE, puis `00 00`.
- `zero n` émet exactement `n` octets nuls; `n` doit être strictement positif.
- `align n` ajoute le minimum d'octets pour aligner l'offset de section sur `n`.
  Le remplissage est `90` dans `.text`, `00` ailleurs.
- `word`, `dword`, `qword` et `addr` doivent commencer sur leur alignement
  naturel. Le seed ne corrige pas implicitement un mauvais alignement.

## 4. Registres et adressage

Les seuls registres sont les GPR 64 bits :

```text
rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
```

Leur code est respectivement `0..15`; `lo3(reg)` donne les trois bits bas et le
bit 3 alimente le bit REX approprié. `rip` n'est pas un registre général : il est
valide uniquement dans `[rip + label]`.

Formes mémoire autorisées :

```text
[base]
[base + déplacement]
[base - déplacement]
[base + index * échelle]
[base + index * échelle + déplacement]
[base + index * échelle - déplacement]
[rip + label]
```

`base` peut être tout GPR. `index` ne peut être ni `rsp` ni `r12`. Il n'existe
ni adresse absolue, ni forme sans base, ni segment override. Un déplacement base
est choisi canoniquement : absent si zéro et encodable, sinon `disp8` s'il tient
dans `i8`, sinon `disp32`. Les bases `rbp` et `r13` avec déplacement nul utilisent
obligatoirement `mod=01, disp8=00`. Les bases `rsp` et `r12` utilisent toujours un
SIB. Une référence RIP utilise `mod=00, r/m=101, disp32`, calculé depuis l'octet
suivant l'instruction complète.

Toutes les branches sont `rel32`; NTASM-S0 ne fait aucune relaxation. Le
déplacement est `RVA(cible) - RVA(octet_suivant)` et doit tenir dans `i32`.

## 5. Jeu d'instructions fermé

NTASM-S0 reconnaît exactement les 30 mnémoniques ci-dessous. `rd` et `rs` sont
des GPR; `mem` est une forme mémoire autorisée; `imm32` est signé; `imm8` est
non signé; `label` est un label `.text`. `/r` signifie ModRM avec `reg=rs` ou
`reg=rd` selon la ligne et `r/m` égal à l'autre opérande. `id`, `ib`, `cd` et
`io` sont des valeurs little-endian de 32, 8, 32 et 64 bits.

| Mnémonique et formes | Encodage canonique | Effet / contrainte |
|---|---|---|
| `mov rd, rs` | `REX.W 89 /r` (`reg=rs`) | copie 64 bits |
| `mov rd, imm64` | `REX.W B8+rd io` | immédiat numérique seulement, toujours 64 bits |
| `lea rd, mem` | `REX.W 8D /r` (`reg=rd`) | adresse effective 64 bits |
| `load8 rd, mem` | `REX 0F B6 /r` | charge 8 bits, zéro-extension; REX toujours présent |
| `load16 rd, mem` | `REX 0F B7 /r` | charge 16 bits dans `rd32`; l'écriture 32 bits zéro-étend le résultat vers le GPR 64 bits; aucun préfixe `66` |
| `load32 rd, mem` | `REX 8B /r` | charge 32 bits, zéro-extension x86 |
| `load64 rd, mem` | `REX.W 8B /r` | charge 64 bits |
| `store8 mem, rs` | `REX 88 /r` | écrit les 8 bits bas; REX toujours présent |
| `store16 mem, rs` | `66 REX 89 /r` | écrit les 16 bits bas |
| `store32 mem, rs` | `REX 89 /r` | écrit les 32 bits bas |
| `store64 mem, rs` | `REX.W 89 /r` | écrit 64 bits |
| `add rd, rs` / `add rd, imm32` | `REX.W 01 /r` / `REX.W 81 /0 id` | addition 64 bits |
| `sub rd, rs` / `sub rd, imm32` | `REX.W 29 /r` / `REX.W 81 /5 id` | soustraction 64 bits |
| `and rd, rs` / `and rd, imm32` | `REX.W 21 /r` / `REX.W 81 /4 id` | ET 64 bits |
| `or rd, rs` / `or rd, imm32` | `REX.W 09 /r` / `REX.W 81 /1 id` | OU 64 bits |
| `xor rd, rs` / `xor rd, imm32` | `REX.W 31 /r` / `REX.W 81 /6 id` | XOR 64 bits |
| `cmp rd, rs` / `cmp rd, imm32` | `REX.W 39 /r` / `REX.W 81 /7 id` | drapeaux de `rd-rhs` |
| `test rd, rs` | `REX.W 85 /r` | drapeaux de `rd&rs` |
| `shl rd, imm8` | `REX.W C1 /4 ib` | décalage logique, compte `0..63` |
| `shr rd, imm8` | `REX.W C1 /5 ib` | décalage logique, compte `0..63` |
| `imul rd, rs, imm32` | `REX.W 69 /r id` | produit signé bas 64 bits |
| `push rs` | `[REX.B] 50+rs` | pas de préfixe REX neutre |
| `pop rd` | `[REX.B] 58+rd` | pas de préfixe REX neutre |
| `call label` / `call rs` | `E8 cd` / `[REX.B] FF /2` | appel relatif ou indirect registre |
| `ret` | `C3` | aucun opérande |
| `jmp label` | `E9 cd` | saut relatif 32 bits |
| `je label` | `0F 84 cd` | ZF=1 |
| `jne label` | `0F 85 cd` | ZF=0 |
| `jb label` | `0F 82 cd` | comparaison non signée `<` |
| `jae label` | `0F 83 cd` | comparaison non signée `>=` |
| `nop` | `90` | aucun opérande |

Pour les formes dont le tableau porte `REX.W`, `W=1`. `push`, `pop` et l'appel
indirect ont une taille 64 bits implicite et n'émettent que `REX.B` pour
`r8..r15`. Pour les chargements/stockages 8, 16 et 32 bits, un préfixe REX est
toujours émis, même s'il vaut `40`, afin d'éviter les registres hauts historiques
et de garder une règle unique. `REX.R` étend le champ ModRM `reg`, `REX.X` étend
l'index SIB et `REX.B` étend `r/m` ou la base. Aucun autre préfixe, opcode
équivalent ou encodage raccourci n'est permis.

### 5.1 Drapeaux et recouvrements

`add`, `sub`, `and`, `or`, `xor`, `cmp`, `test`, `shl`, `shr` et `imul`
produisent les drapeaux x86 habituels. Le langage ne garantit leur valeur que si
la prochaine instruction exécutée est une branche conditionnelle; des labels
peuvent séparer textuellement les deux, mais aucune instruction, même `nop`, ne
doit s'interposer. Les charges et stores suivent l'ordre mémoire x86 normal; S0
ne fournit ni atomique, ni barrière, ni I/O port, ni instruction privilégiée.

## 6. ABI `efi_x64`

Le writer n'insère aucun prologue, épilogue, shadow space ou restauration. La
source est responsable des règles suivantes :

- entrée PE : `rcx = ImageHandle`, `rdx = SystemTable`;
- arguments entiers/pointeurs : `rcx`, `rdx`, `r8`, `r9`, puis pile;
- résultat `EFI_STATUS` : `rax`;
- volatils : `rax`, `rcx`, `rdx`, `r8`, `r9`, `r10`, `r11`;
- non volatils : `rbx`, `rbp`, `rdi`, `rsi`, `r12..r15` et `rsp`;
- avant chaque `call`, `rsp` est aligné sur 16 octets et le caller réserve au
  moins 32 octets de shadow space;
- au retour ou si `BootServices.Exit` rend la main, `rsp` et tous les registres
  non volatils modifiés sont restaurés;
- la direction de chaîne doit rester vers l'avant; S0 n'émet aucune instruction
  de chaîne et ne doit pas dépendre de DF.

La déclaration `abi efi_x64` atteste ce contrat mais S0 ne prouve pas
statiquement l'alignement de pile. Cette limite doit apparaître dans la revue de
`ntasm1.ntasm`.

## 7. Passes, symboles et déterminisme

Le seed suit exactement deux passes sur les mêmes octets source :

1. **Pass 1 — mesure et symboles.** Validation lexicale/syntaxique, calcul de
   chaque taille d'instruction, définition des offsets de labels, comptage des
   fixups et des relocations. Aucun octet de sortie n'est écrit.
2. **Layout PE.** Calcul des RVA, tailles brutes et champs d'en-tête selon
   `seed-pe32.md`.
3. **Pass 2 — émission.** Nouvelle analyse complète, émission des sections,
   résolution `rel32`/RIP et des `addr`, construction de `.reloc`, puis écriture
   des en-têtes. Les compteurs et tailles doivent être identiques à la Pass 1.

Une différence de taille ou de token entre les passes est une erreur interne.
Il n'existe ni table de hachage à ordre variable, ni timestamp, ni chemin hôte,
ni opcode choisi selon une heuristique. Les symboles sont conservés dans l'ordre
de première définition; les relocations sont triées par RVA du champ à corriger.

## 8. Limites obligatoires

| Ressource | Limite |
|---|---:|
| source acceptée | `1..65 535` octets |
| lignes physiques | `4 096` |
| longueur d'une ligne, EOL exclu | `255` octets |
| tokens sur une ligne | `16` |
| longueur d'un identifiant | `31` octets |
| longueur décodée d'une chaîne | `1 024` octets |
| symboles constants + labels | `512` |
| fixups branche/RIP | `2 048` |
| relocations `DIR64` | `512` |
| instructions | `8 192` |
| taille virtuelle `.text` | `32 768` octets |
| taille virtuelle `.rdata` | `16 384` octets |
| taille virtuelle `.data` | `8 192` octets |
| taille virtuelle `.reloc` générée | `4 096` octets |
| image PE finale | `65 536` octets |

Chaque addition, multiplication, alignement et conversion est vérifié avant
l'opération. Atteindre exactement une limite est valide, sauf la taille source
où `65 536` est volontairement la sentinelle « trop grand ». Un dépassement
arrête l'assemblage sans troncature. Les maxima de section donnent un fichier B1
maximal de `0xF400` octets et un `SizeOfImage` maximal de `0x10000`; ils restent
donc compatibles avec la limite de `0x10000` octets du fichier.

Les limites lexicales locales — longueur de ligne, tokens d’une ligne,
identifiant et chaîne décodée — produisent `E101` au premier élément
excédentaire. Les limites globales — taille source, nombre de lignes, tables de
symboles/fixups/relocations, nombre d’instructions et tailles utiles de sections
— produisent `E500`. La limite du fichier PE final reste `E601`, car elle porte
sur la représentabilité du conteneur après layout.

## 9. Diagnostics stables

La première erreur dans l'ordre des octets source gagne. Une ligne de diagnostic
est émise en UTF-16 par `OutputString` sous la forme :

```text
NGS0 Eddd Lhhhh Chhhh Ohhhhhhhh\r\n
```

`ddd` est le code décimal à trois chiffres; ligne et colonne sont des valeurs
hexadécimales sur quatre chiffres, commençant à 1; `O` est l'offset byte hexadécimal
sur huit chiffres, commençant à 0. Une erreur sans position source utilise zéro
pour L, C et O. Aucun texte localisé ou nom de chemin variable n'est ajouté.

Les positions de frontière physique sont figées :

- source vide : `E100 L0000 C0000 O00000000`;
- cumul atteignant la sentinelle `65 536` :
  `E500 L0000 C0000 O00000000`, avant scanner;
- ligne non vide sans EOL : `E100` à l’EOF virtuel, avec
  `L = ligne courante`, `C = longueur hors EOL + 1` et `O = taille source`;
- 256e octet hors EOL : `E101` sur cet octet, colonne `0x0100`;
- premier octet d’une 4 097e ligne : `E500` sur cet octet, ligne `0x1001` et
  colonne `1`.

| Code | Signification |
|---:|---|
| E001 | source introuvable ou protocole fichier indisponible |
| E002 | information, type répertoire, lecture ou fermeture source invalide |
| E100 | octet source, BOM, NUL ou fin de ligne invalide |
| E101 | ligne, token, identifiant ou chaîne trop long |
| E102 | entier ou échappement lexical invalide |
| E200 | ordre du préambule ou des sections invalide |
| E201 | syntaxe, ponctuation ou nombre d'opérandes invalide |
| E202 | instruction/directive interdite dans cette section |
| E300 | symbole dupliqué |
| E301 | symbole absent ou mauvaise catégorie de symbole |
| E302 | `entry` absent, non `.text` ou non placé sur une instruction |
| E400 | mnémonique ou forme d'opérandes non supportée |
| E401 | immédiat, déplacement ou `rel32` hors plage |
| E402 | adressage, index ou alignement naturel invalide |
| E500 | une limite de ressource est dépassée |
| E600 | taille/fixup incohérent entre les deux passes |
| E601 | PE final supérieur à la limite ou champ PE non représentable |
| E602 | aucune relocation `DIR64` produite |
| E603 | corruption interne d’un canari de pool |
| E604 | échec interne de cleanup `FreePool` |
| E700 | sortie déjà existante; aucun écrasement autorisé |
| E701 | création du fichier de sortie impossible |
| E702 | écriture courte, `Flush` ou `Close` en erreur |

Les erreurs de source et de capacité quittent avec `EFI_LOAD_ERROR`; les erreurs
d'allocation avec `EFI_OUT_OF_RESOURCES`; les erreurs de fichier propagent le
statut UEFI si celui-ci est un statut d'erreur, sinon `EFI_DEVICE_ERROR`.
`E603` est toujours interne, n’accuse jamais la source, utilise une position
entièrement nulle, quitte avec `EFI_LOAD_ERROR` et remplace tout diagnostic
antérieur, puisque l’intégrité de l’état n’est plus démontrée. `E604` utilise
également une position entièrement nulle, mais n’est sélectionné que si aucun
diagnostic antérieur n’existe et si au moins un `FreePool` échoue. Son
`ExitStatus` propage le premier statut EFI en erreur rendu par `FreePool`; un
statut incohérent qui n’est pas une erreur EFI devient `EFI_DEVICE_ERROR`.
Les erreurs `Close` et E603 sont sélectionnées avant l’affichage du diagnostic
primaire. E603 contourne le formateur et appelle directement `OutputString` sur
sa chaîne statique `.rdata`, sans consulter `WORK`; tous les autres diagnostics
sont formatés pendant que le scratch `WORK` existe. Les deux chemins posent le
même flag « ligne tentée ». Après les tentatives `FreePool`, si aucun diagnostic
antérieur n’existait, qu’aucune ligne n’a été
tentée et qu’E604 vient d’être sélectionné, sa chaîne UTF-16 statique en `.rdata`
est affichée avant `Exit`.
Un diagnostic antérieur interdit cette seconde ligne.

## 10. Vecteurs golden d'encodage

Les espaces ci-dessous séparent des octets hexadécimaux. Ils sont normatifs.

| Source NTASM-S0 | Octets attendus |
|---|---|
| `mov rax, 0x1122334455667788` | `48 B8 88 77 66 55 44 33 22 11` |
| `mov r12, rsp` | `49 89 E4` |
| `lea r11, [r12 + rbx * 4 + 0x20]` | `4D 8D 5C 9C 20` |
| `load8 rax, [rsi]` | `40 0F B6 06` |
| `load16 r9, [r13]` | `45 0F B7 4D 00` |
| `load64 rax, [rdx + 0x40]` | `48 8B 42 40` |
| `store8 [rdi + 1], rax` | `40 88 47 01` |
| `store32 [rsp + 8], r10` | `44 89 54 24 08` |
| `add r8, -1` | `49 81 C0 FF FF FF FF` |
| `imul rax, rax, 10` | `48 69 C0 0A 00 00 00` |
| `push r12` puis `pop r12` | `41 54 41 5C` |
| `call next` avec `next` à l'octet suivant | `E8 00 00 00 00` |
| `jne loop` à RVA `0x1000`, instruction à `0x1000` | `0F 85 FA FF FF FF` |
| `lea rdx, [rip + msg]` avec `msg` à l'octet suivant | `48 8D 15 00 00 00 00` |

### 10.1 Golden de layout minimal

Pour une image dont `.text` contient huit octets — un `lea rdx,[rip + msg]`
suivi de `ret` —, dont l'ancre `qword addr(start)` est à l'offset zéro de
`.rdata`, dont `msg` suit cette ancre et vaut `utf16z "NG"`, et dont `.data`
contient huit octets nuls, les valeurs suivantes sont exigées :

```text
.text   RVA 0x1000, raw 0x0400, payload 48 8D 15 01 10 00 00 C3
.rdata  RVA 0x2000, raw 0x0600,
        payload 00 10 00 40 01 00 00 00 4E 00 47 00 00 00
.data   RVA 0x3000, raw 0x0800, payload 00 00 00 00 00 00 00 00
.reloc  RVA 0x4000, raw 0x0A00,
        payload 00 20 00 00 0C 00 00 00 00 A0 00 00
entry   RVA 0x1000
image   0x0C00 octets, SizeOfImage 0x5000
```

L'ancre vaut `0x0000000140001000` en little-endian. L'entrée de relocation
`A000` signifie `DIR64` à l'offset zéro de la page RVA `0x2000`; le dernier
`0000` est une entrée `ABSOLUTE` de padding.

## 11. Corpus négatif minimal

Avant d'implémenter une forme, le corpus doit au moins couvrir : octet non ASCII,
CR isolé, ligne trop longue, entier débordant, section manquante ou réordonnée,
instruction dans `.rdata`, donnée dans `.text`, symbole dupliqué ou absent,
branche hors `.text`, `rsp`/`r12` comme index, déplacement hors `i32`, immédiat
hors plage, `qword addr` mal aligné, absence de relocation, source et sortie trop
grandes, sortie préexistante et écriture courte simulée. Chaque fixture attend un
code unique de la table précédente.

## 12. Non-objectifs de S0

S0 ne fournit pas : macros, includes, expressions générales, labels locaux,
visibilité, modules, conditionnels d'assemblage, répétitions, symboles externes,
imports PE, linker, objets relogeables, debug/unwind, flottants, SIMD, atomiques,
privilèges, ports I/O, segments, TLS, exceptions, optimisations, relaxation de
branches, encodage choisi par coût, syntaxe Intel complète ou compatibilité avec
un assembleur étranger.

Ajouter une instruction ou une directive à S0 exige d'abord un golden positif,
un cas négatif, un budget d'octets seed et une révision explicite de cette
spécification. Si le seed dépasse `64 Kio`, la réponse est de réduire S0, pas de
faire entrer un producteur étranger dans la chaîne.
