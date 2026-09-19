# NTASM v0 — langage machine typé de Nexora Genesis

Statut : spécification conceptuelle normative initiale, version `NTASM-v0`.

Jalon de livraison fixé le 4 septembre 2026 : **B2.1 = NTASM v0 complet**,
suivant [le contrat A1–A10](../docs/ntasm-completion.md) et l'ADR 0011.
L'ancien ordre « dernier frontend après NX/Nova/OS » est remplacé.
Les prérequis de gel de la section 17 restent à satisfaire avant les
comportements v0; le simple auto-hébergement du profil S0 ne livre pas v0.

Ce document fixe la direction du NTASM final : un langage source machine typé,
structuré, vérifiable et intégré à la chaîne commune Genesis. Il ne constitue
pas encore le catalogue exhaustif des instructions, la grammaire complète ou le
format binaire final. Les formes de surface et invariants explicitement marqués
ici sont normatifs; les détails encore ouverts doivent être figés par une
révision avant implémentation.

## 1. Deux langages de portée différente

`NTASM-S0` et `NTASM v0` ne sont pas deux noms pour le même profil.

| Propriété | NTASM-S0 | NTASM v0 |
|---|---|---|
| Rôle | bootstrap temporaire fermé | langage machine typé final |
| Autorité | [`ntasm-s0.md`](ntasm-s0.md) | ce document et ses futures révisions |
| Producteur initial | `seed0.efi` saisi en octets | frontend de la chaîne Genesis auto-hébergée |
| Syntaxe | lignes plates, trois sections fixes, 30 mnémoniques | modules, sections structurées, fonctions et contrats typés |
| Types | registres GPR et largeurs imposées par les formes | scalaires, registres, features CPU, pointeurs par espace et ABI |
| Métaprogrammation | aucune | comptime borné et macros AST hygiéniques sans I/O |
| Sortie | PE32+ `B1-PE-v1` | NTIR, Machine IR, NXO et PE selon profils versionnés |
| Durée de vie | jusqu’au bootstrap et au point fixe | maintenance durable du système |

**Règle de non-contamination :** S0 ne reçoit aucun module, fonction typée,
`effects`, `clobbers`, `requires`, `ensures`, pointeur par espace, macro,
comptime, génération de stub ou intrinsic supplémentaire défini ici. Toute
tentative de rétroporter une feature v0 dans S0 exige une révision explicite de
`ntasm-s0.md`, ses goldens, son budget d’octets et ses preuves; la décision par
défaut est le refus.

Les Gates B1 et B2 restent donc jugées exclusivement par NTASM-S0. NTASM v0 ne
peut ni détendre leurs bornes ni servir d’excuse pour agrandir le seed.

## 2. Objectifs de conception

NTASM v0 doit permettre d’écrire les portions du système qui exigent un contrôle
machine exact sans revenir à un assembleur textuel non typé.

Il doit :

- rendre visibles le target, l’ABI, les registres d’entrée, les clobbers, les
  effets, les préconditions et les postconditions;
- imposer une largeur explicite ou inférable sans ambiguïté;
- utiliser l’ordre d’opérandes `destination, source`;
- distinguer les espaces d’adresses dans les types de pointeurs;
- modéliser les registres, flags, features et intrinsics CPU par target;
- produire des symboles, sections et relocations versionnés et déterministes;
- générer les stubs répétitifs d’interruption/syscall depuis des déclarations
  structurées, pas depuis du texte concaténé;
- converger avec NX vers NTIR puis Machine IR sans imposer à NX de produire un
  fichier `.ntasm` intermédiaire;
- rester auditable : aucune capacité privilégiée implicite, aucune macro avec
  I/O, aucune évaluation comptime non bornée.

NTASM v0 n’est pas une syntaxe Intel générale, un préprocesseur de texte, un
linker universel ou une voie pour contourner les effets et capacités de Cnuva,
NX ou Nova.

## 3. Forme d’un module

Un fichier NTASM v0 contient exactement un module et exactement un target. Leur
ordre est figé : `module`, puis `target`, puis les déclarations et sections.

```text
module <nom.qualifié>
target <triple-cible>
```

- Le nom de module est stable, sensible à la casse et indépendant du chemin
  hôte. Deux modules ne peuvent partager le même nom dans une unité de build.
- Le target est une valeur enregistrée, telle que `x86_64-nexora-none`; il
  sélectionne le registre typé, les features, les intrinsics, l’ABI par défaut et
  les relocations disponibles.
- Un target inconnu ou une feature absente est une erreur avant Machine IR.
- Le profil initial accepte exactement `x86_64-nexora-none` et
  `x86_64-nexora-uefi`.
- Le catalogue x86_64 v0 est fermé à `lm`, `msr`, `syscall`, `sse2`, `tsc`.
  `lm` est implicite et idempotent; les autres doublons et les noms qualifiés ou
  inconnus sont refusés.
  Tout prédicat `feature(x)` dans `requires` ou `ensures` doit appartenir à ce
  catalogue et au masque effectivement déclaré par le module; le contrat ne
  peut pas fabriquer une capacité target.
- Le chemin absolu du fichier, l’heure, la locale et l’ordre du système de
  fichiers ne participent jamais à la sortie.

Les versions des contrats binaires sont des métadonnées du module compilé :

- ABI : `nx64-abi-v0` ou ABI target explicitement enregistrée;
- sections : `nxo-sections-v0`;
- symboles : `nxo-symbols-v0`;
- relocations x86_64 : `nxo-x64-reloc-v0`;
- NTIR et Machine IR : version majeure/mineure déclarée.

Le bloc `versions` v0, lorsqu'il est présent, contient exactement une entrée
pour `ntir`, `machine_ir`, `sections`, `symbols`, `relocations` et `nxo`, toutes
en version `0.1`. Un domaine inconnu, dupliqué, incomplet ou d'une autre version
est refusé.

Une incompatibilité de version est une erreur; elle ne déclenche jamais un
fallback silencieux.

## 4. Sections structurées et symboles

Une section est un bloc syntaxique, pas un changement d’état implicite qui se
propage jusqu’à la fin du fichier :

```text
section .syscall {
    ...
}
```

Chaque nom de section possède une classe, des permissions et une politique de
relocation définies par le profil de sortie. Une section source logique telle que
`.syscall` peut être regroupée dans une section exécutable du format final, mais
le mapping est déclaré et déterministe. Le compilateur refuse toute combinaison
W+X et toute permission plus large que celle du contenu.

Les symboles sont :

- locaux au module par défaut;
- visibles hors module uniquement avec `export`;
- importés uniquement par une déclaration typée et une version d’ABI compatible;
- triés dans un ordre canonique indépendant d’une table de hachage;
- référencés symboliquement jusqu’à la phase de relocation.

`export fn` exporte une fonction et son contrat, pas seulement une adresse brute.
Le nom, le type, l’ABI, les paramètres liés aux registres, les effets,
préconditions, postconditions et clobbers participent à l’identité d’interface.

## 5. Fonctions, paramètres et registres typés

La forme normative d’une fonction machine est :

```text
[export] fn nom(
    <direction> paramètre: type @registre,
    ...
) -> type_retour
effects { ... }
clobbers { ... }
requires { ... }
ensures { ... } {
    ...
}
```

Les directions initiales sont `in`, `out` et `inout`. Une liaison `@registre`
est une contrainte vérifiée, pas un commentaire : largeur, classe de registre et
type doivent être compatibles. Deux paramètres simultanément vivants ne peuvent
occuper le même registre sauf alias explicite prévu par l’ABI.

Le profil exécutable initial reconnaît exactement `nx64-abi-v0`, explicitement
sur la fonction/import ou comme `default_abi`; son absence sélectionne ce défaut
pour les targets x86_64 Nexora actuels. Il accepte au plus quatorze paramètres
registre. Les quatre premiers paramètres `in` sans liaison explicite utilisent,
dans l’ordre, `rcx`, `rdx`, `r8`, `r9`; le cinquième et les suivants exigent un
`@GPR`. Tout paramètre `out` ou `inout` exige aussi un registre explicite et son
type doit être un entier machine ou un pointeur. Un pointeur est lié à une forme
64 bits. Les alias d’une même famille physique ne créent jamais deux emplacements
ABI distincts.
Une sortie ne peut utiliser rax, rsp ou rbp et sa famille ne peut pas figurer
simultanément dans `clobbers`.
Au call, les paramètres `in` et `inout` consomment un argument dans l’ordre de
la signature; un paramètre `out` n’en consomme aucun.
Chaque argument fourni est vérifié contre le type du paramètre : littéral borné,
paramètre/local du caller ou résultat de call déjà typé. Une expression dont le
type n’est pas encore prouvé échoue fermée.

Le catalogue scalaire v0 est fermé à `u8/u16/u32/u64`, `i8/i16/i32/i64`,
`bool` et `never`. Les formes composées sont `ptr`, `array`, les listes méta et
les variables de type macro prévues par la grammaire. Un tableau de longueur
zéro ou un nom scalaire inconnu est refusé.

Le registre est une valeur target typée. Sur x86_64, `rax` n’est pas un simple
identifiant interchangeable avec un registre SIMD, un segment ou un control
register. Le catalogue target décrit :

- classe et largeur;
- alias de sous-registres;
- rôle spécial éventuel, notamment pile, instruction pointer et flags;
- feature CPU requise;
- privilège et effets de lecture/écriture;
- règles d’allocation et de préservation de l’ABI.

`never` signifie que le contrôle ne revient pas au caller. Un transfert par
`sysretq`, `iretq`, saut terminal ou arrêt vérifié peut satisfaire `-> never`;
une chute à la fin du bloc est une erreur.

Une fonction à valeur doit terminer par un `return` portant une expression, ou
par un `if` dont les deux branches sont terminales. Une fonction `never` refuse
tout `return` et doit terminer par un transfert reconnu. Le premier profil Nova
reconnaît directement `ud2`, `sysretq` et `iretq`; les calls vers une fonction
`never`. Un call statement final résolu vers une fonction locale ou un import
`-> never` est également terminal; les graphes plus généraux restent une
extension séparée.

La condition d’un `if` doit être prouvée `bool`. Le profil initial prouve les
littéraux booléens, les paramètres et locals de type `bool`, les comparaisons,
les calls locaux/importés dont le retour est `bool` et le prédicat target
`feature(x)`. Un entier, un nom d’un autre type ou une forme d’expression dont
le type n’est pas encore inféré est refusé. La résolution et l’arité des calls
sont contrôlées avant ce test de type afin de conserver leur diagnostic propre.

## 6. Largeur et ordre des opérandes

L’ordre NTASM v0 est toujours `destination, source`. Une instruction à trois
opérandes place la destination en premier, puis les sources dans leur ordre
sémantique. Aucune instruction n’inverse cet ordre pour imiter une autre syntaxe.

Une largeur est valide seulement si elle est :

1. explicite dans le type ou l’intrinsic; ou
2. inférée de manière unique depuis la destination, la source mémoire typée et
   la signature target.

Pour `load`, la largeur du registre destination et celle du type pointé doivent
coïncider. Pour `store`, le type pointé de la destination et la source doivent
coïncider. Pour une opération arithmétique, toutes les valeurs ont la largeur du
type de destination. Un littéral non contraint, deux largeurs contradictoires ou
plusieurs encodages sémantiquement différents produisent un diagnostic; le
compilateur ne choisit jamais selon une préférence implicite de taille.

Chaque `return` est comparé au type déclaré. Le premier vérificateur prouve les
littéraux bornés, les paramètres/locals et les résultats de calls de type exact;
une expression plus complexe reste refusée tant que son inférence n’est pas
portée. `bool` n’accepte que `true`/`false` et les littéraux entiers respectent
la plage signée ou non signée de leur type.

Les initialiseurs littéraux de `data` et `let` suivent les mêmes règles de
catégorie et de plage scalaire. Le vérificateur Nova v0 contrôle directement
les entiers et `true`/`false`; une expression non littérale est explicitement
différée vers l’inférence d’expressions et n’est jamais considérée comme prouvée
par ce contrôle.

Pour un `let`, le profil initial prouve ensuite les initialiseurs qui sont un
paramètre/local visible ou un call local/importé de type retour exact. Toute
autre expression reste refusée jusqu’à ce que son inférence soit portée; aucune
conversion numérique, pointeur ou booléenne implicite n’est appliquée.

Un `const` scalaire du profil initial exige un littéral compatible et borné.
Les expressions de constante plus générales restent refusées jusqu’au portage
de leur évaluation compile-time; elles ne sont pas rabattues silencieusement
vers une valeur hôte.

L’encodage le plus court peut être sélectionné seulement entre encodages
sémantiquement identiques explicitement autorisés par le profil target. Le choix
est alors canonique et testé byte pour byte.

## 7. Pointeurs par espace

La forme normative d’un pointeur est `ptr<espace, T>`. L’espace fait partie du
type et survit dans NTIR jusqu’à ce qu’un abaissement vérifié le transforme en
adresse machine.

Espaces conceptuels initiaux :

- `user` : mémoire d’un processus utilisateur;
- `kernel` : mémoire virtuelle noyau;
- `physical` : adresse physique non directement déréférençable;
- `mmio` : fenêtre de périphérique, accès volatile et ordonné;
- `device` : mémoire visible d’un device sous une capacité précise;
- `firmware` : pointeurs valides seulement pendant la phase firmware.

Le target peut en ajouter par version. Il n’existe aucune conversion implicite
entre espaces, ni entre entier et pointeur. Une conversion exige un intrinsic
typé, un effet déclaré et une précondition/capacité prouvée. `ptr<physical, T>`
ne devient pas déréférençable sans mapping; `ptr<mmio, T>` n’accepte pas un load
ordinaire si le target exige un intrinsic MMIO.

Sur x86_64, `fs:` et `gs:` sont des décorateurs d’adressage target, pas des
espaces de pointeur. Le symbole et l’annotation `ptr<...>` déterminent toujours
l’espace sémantique. Une annotation de type après un opérande mémoire décrit le
type de l’adresse et de la valeur pointée, jamais un cast silencieux.

## 8. Effets, clobbers et contrats

### 8.1 `effects`

`effects` déclare une borne supérieure vérifiable des effets de la fonction. Le
vocabulaire est fermé et versionné. Le noyau initial comprend au minimum :

- `privileged`;
- `reads_mem` et `writes_mem`, raffinés par les espaces touchés dans NTIR;
- `changes_flags`;
- accès `mmio`, `io_port`, `msr` ou control register;
- changement d’état interrupt/CPU;
- transfert de contrôle sans retour.

Le vocabulaire exact du profil v0, déjà porté par la grammaire et le frontend de
référence, est le suivant :

- effets atomiques sans argument : `privileged`, `changes_flags`, `mmio`,
  `io_port`, `msr`, `control`, `interrupt_state`, `no_return`,
  `memory_ordering`, `reads_clock`, `suspends`;
- effets mémoire : `reads_mem` et `writes_mem`, soit sans argument pour couvrir
  tous les espaces v0, soit avec exactement un espace parmi `user`, `kernel`,
  `physical`, `mmio`, `device`, `firmware`.

Les noms d'effet et d'espace ne sont pas qualifiés. Un argument sur un effet
atomique, un nom inconnu ou un espace hors catalogue est une erreur. Un set
refuse deux occurrences sémantiquement identiques; deux effets mémoire de même
nom mais portant sur deux espaces distincts restent valides. Ces règles ferment
la forme déclarative et son encodage; la preuve qu'un corps et ses appels sont
couverts par cette borne supérieure appartient à la composition décrite
ci-dessous.

Tout effet réel doit être couvert. Un call compose les effets déclarés du callee.
Une macro, un intrinsic ou un stub généré ne peut masquer un effet. Les profils
NX/Cnuva/Nova continuent d’appliquer leurs propres capacités; passer par NTASM
n’accorde aucune autorisation supplémentaire au caller.

Au call, les effets atomiques du callee doivent être un sous-ensemble de ceux du
caller. Il en va séparément des espaces lus et écrits : chaque bit d'espace du
callee doit être déclaré par le caller. Un `reads_mem` ou `writes_mem` sans
espace couvre les six espaces v0; une forme bornée ne couvre que son espace.
Cette composition s'applique de la même façon aux fonctions locales et aux
alias d'import, ainsi qu'aux calls statements et expressions.

Dans le corps, `load` ajoute un effet de lecture sur l’espace du `ptr<espace,T>`
de son opérande mémoire et `store` ajoute l’effet d’écriture correspondant. Le
contrat de la fonction doit couvrir ce bit d’espace, directement ou par la forme
mémoire non bornée. Un espace de pointeur hors catalogue est refusé avant NTIR.

Les effets intrinsèques fixes suivent le catalogue de conformance x86_64 :
`rdtsc` exige `reads_clock`; les fences exigent `memory_ordering`; `ud2` exige
`no_return`; `cli`/`sti` exigent `privileged` et `interrupt_state`; `hlt` exige
`privileged` et `suspends`; `swapgs`, les control registers et les opérations de
descripteurs exigent `privileged` et `control`; MSR exige `privileged` et `msr`;
I/O port exige `privileged` et `io_port`. `syscall` exige `control` et
`changes_flags`; `sysretq`/`iretq` ajoutent `privileged` et `no_return`.
Les opérations arithmétiques, `cmp`/`test`, shifts/rotations, multiplications,
divisions et bit operations exigent également `changes_flags`; `mov` et `xchg`
ne l'exigent pas par leur seule exécution.

### 8.2 `clobbers`

`clobbers` est l’ensemble des registres et états machine dont la valeur d’entrée
n’est pas préservée au point de retour ou de transfert. Les écritures directes,
les appels et les intrinsics générés sont composés puis comparés à la déclaration.

Les modifications temporaires sont suivies séparément dans un **write footprint**
inféré par la HIR/NTIR. Elles n'appartiennent à `clobbers` que si la valeur
d'entrée n'est pas restaurée à la frontière. Ainsi, modifier temporairement
`rsp` puis restaurer exactement sa valeur ne déclare pas `rsp` comme clobber;
omettre cette écriture du write footprint interne reste impossible.

Les paramètres `out` et les résultats liés à un registre sont distingués des
clobbers, mais restent vérifiés par l’ABI. Omettre un clobber est une erreur;
ajouter un clobber non utilisé peut être signalé comme contrat trop large.

Dans le profil x86_64 v0, chaque entrée de `clobbers` est un nom de registre non
qualifié appartenant au catalogue target. Les alias (`rax`/`eax`, `r10`/`r10d`,
etc.) désignent la même famille physique et un set ne peut la déclarer deux fois.
Au call, le bitset des familles clobber du callee doit être un sous-ensemble de
celui du caller, pour les fonctions locales comme pour les alias d'import et les
calls expressions.

Un `load` écrit sa destination GPR : la famille physique de ce registre doit
donc figurer dans les clobbers de la fonction. Les alias désignent le même bit.
`store` ne clobber pas son registre source par cette seule opération.
Les footprints fixes initiaux sont : `rdtsc` et `rdmsr` détruisent rax/rdx;
`cpuid` détruit rax/rbx/rcx/rdx; `syscall` détruit rcx/r11; `iretq` modifie rsp.
Ces familles doivent également être incluses, alias compris.
Les formes qui écrivent explicitement leur premier opérande GPR (`mov`, `lea`,
arithmétique, shifts/rotations, bit operations, bsf/bsr, read_cr et in) ajoutent
sa famille au footprint. `xchg` ajoute les deux familles. `cmp` et `test`
n’écrivent aucun opérande.

### 8.3 `requires` et `ensures`

Les contrats utilisent des prédicats typés enregistrés, pas du texte arbitraire.
`requires` décrit les faits nécessaires à l’entrée; `ensures` les faits établis
au retour ou au transfert terminal. Exemples de familles de prédicats : feature
CPU disponible, pointeur canonique/non nul, stack dans un espace donné et alignée,
MSR initialisé, interruptions dans un état précis, mapping présent ou contexte de
retour utilisateur valide.

Le catalogue déclaratif exact du profil v0 est : `feature(x)`, `non_null(x)`,
`canonical(x)`, `stack_in(espace)`, `stack_aligned(n)`, `msr_initialized(x)`,
`interrupts_enabled()`, `interrupts_disabled()`, `mapping_present(x)` et
`valid_user_return(rip, rsp)`. Les noms de prédicat ne sont pas qualifiés. Les
arités sont donc respectivement1,1,1,1,1,1,0,0,1,2; tout autre nom ou nombre
d’arguments est une erreur avant preuve. Cette fermeture du catalogue et de la
forme AST ne signifie pas que chaque famille possède déjà ses règles de preuve :
une famille reconnue mais non implémentée reste refusée par le vérificateur de
faits plutôt que supposée vraie.

Au call, l’état symbolique du caller doit satisfaire `requires`; il est ensuite
mis à jour par `ensures`, les effets et les clobbers. v0 n’exige pas un prouveur
général : les prédicats acceptés et leurs règles de transfert forment un ensemble
fermé, déterministe et versionné. Un contrat inconnu ou non prouvé est une erreur.

Le premier vérificateur de calls prouve quatre familles. `feature(x)` est établi
par le masque target du module. `stack_aligned(n)` est établi par le même fait
du caller ou par la garantie ABI lorsque `n` est une puissance de deux au plus16.
`non_null(p)` et `canonical(p)` remappent le paramètre du callee vers l’argument
du call; cet argument doit nommer un paramètre du caller portant le même fait.
Une expression littérale ou calculée n’est pas présumée sûre. Les six autres
familles reconnues restent refusées aux calls tant que leur transfert n’est pas
implémenté. Cette règle conservatrice évite qu’un prédicat connu soit traité
comme prouvé par défaut.

Pour une fonction `never`, `ensures` décrit l’état au point de transfert, pas un
retour inexistant.

## 9. Exemple syntaxique autoritatif x86_64

Le snippet suivant est conservé **mot pour mot** comme référence de forme fournie
par le porteur du projet. Il est normatif pour la surface syntaxique montrée,
mais pas encore pour la sûreté ou l'ABI complète d'un retour syscall.

```ntasm
module kernel.syscalls
target x86_64-nexora-none

section .syscall {
    export fn syscall_entry(
        in number: u64 @rax,
        in arg0: u64 @rdi,
        in arg1: u64 @rsi
    ) -> never
    effects { privileged, reads_mem, writes_mem, changes_flags }
    clobbers { rax, rcx, r11 } {
        swapgs
        load rsp, [gs:kernel_rsp]:ptr<kernel, u64>
        align_stack 16
        call nx_syscall_dispatch
        sysretq
    }
}
```

Cet exemple fixe :

- `load`/`store` ont une largeur 64 bits inférée sans ambiguïté depuis le registre
  et le type pointé;
- l’annotation mémoire n’autorise aucun cast d’espace;
- `swapgs` et `sysretq` sont des intrinsics privilégiés x86_64, pas des
  mnémoniques génériques disponibles sur tout target;
- `align_stack 16` agit sur la pile noyau active, produit un fait d’alignement
  dans NTIR et doit être compatible avec l’ABI de `nx_syscall_dispatch`.

Il ne prouve pas encore que `sysretq` est sûr : la sauvegarde/restauration du
RSP/RCX/R11 utilisateur, le second `swapgs`, la canonicalité du RIP/RSP, le
masquage des flags dangereux et le contrat du dispatcher restent à définir dans
l'ABI syscall. Ce bloc ne doit donc pas être exécuté tel quel. Une future variante
durcie sera un exemple séparé et ne remplacera ce snippet qu'après décision
explicite du porteur du projet.

## 10. `align_stack` et intrinsics privilégiés

`align_stack N` est un intrinsic de vérification et d’abaissement, pas une macro
textuelle. `N` doit être une puissance de deux autorisée par l’ABI target. Il :

- exige un `rsp` typé comme pile dans l’espace courant;
- ajuste vers le bas de façon checked;
- produit le fait `stack_aligned(N)`;
- invalide les offsets de pile antérieurs non suivis par le système de frame;
- doit être compensé avant tout retour normal, sauf si la fonction est `never`
  et remplace explicitement la pile avant son transfert terminal.

Les intrinsics privilégiés forment un catalogue fermé par target. Le catalogue
x86_64 pourra inclure notamment `swapgs`, `sysretq`, `iretq`, lecture/écriture
MSR, control registers, tables de descripteurs, invalidation TLB et I/O port.
Chaque entrée déclare :

- types d’opérandes et de résultats;
- features CPU requises;
- niveau de privilège;
- effets et clobbers;
- préconditions et postconditions;
- séquence Machine IR/encodage canonique.

Un nom d’intrinsic inconnu, une feature non prouvée ou un contexte non privilégié
est refusé avant émission. Un target ne peut accepter silencieusement
l’instruction d’un autre target.

## 11. Stubs structurés d’interruption et syscall

Les familles répétitives de stubs sont générées depuis des déclarations AST
typées. Une déclaration de vecteur/interruption/syscall doit fournir au minimum :

- identifiant et numéro ou classe;
- ABI d’entrée et de sortie;
- paramètres matériels implicites liés aux registres ou à la frame;
- présence d’un error code matériel;
- registres sauvegardés, clobbers et stack alignment;
- politique de `swapgs`, masquage d’interruptions et transfert terminal;
- handler cible et son contrat;
- section, visibilité et symboles à générer.

Le générateur produit des nœuds NTIR, des symboles et relocations typés. Il ne
concatène pas du texte NTASM et ne réinvoque pas le lexer. Les stubs d’une table
sont ordonnés par clé numérique canonique; les doublons et trous interdits sont
diagnostiqués avant émission.

Une déclaration structurée ne dispense pas de goldens byte pour byte et de cas
négatifs pour frame, privilège, clobbers, alignement et retour.

## 12. Comptime borné

Le comptime v0 est un évaluateur déterministe sur valeurs et AST typés. Chaque
unité de build possède des limites explicites et vérifiées avant incrément :

- fuel d’instructions;
- profondeur d’appel;
- mémoire d’arène;
- nombre de nœuds AST produits;
- profondeur d’expansion;
- taille totale des données constantes.

Atteindre la limite est permis si l’opération est complète; `+1` est une erreur
stable. Une récursion n’est autorisée que si elle termine dans le fuel. Une
évaluation ne peut observer ni fichiers, réseau, environnement, horloge,
aléatoire, locale, processus, adresse hôte ou état mutable externe.

Les entrées autorisées sont les littéraux, paramètres comptime, métadonnées
target/ABI versionnées, types, AST du module et tables déclaratives incluses dans
le graphe de build. Même entrée + mêmes versions + mêmes limites doit produire
les mêmes nœuds et les mêmes diagnostics.

Les valeurs comptime ne donnent aucune capacité runtime. Elles ne peuvent créer
un pointeur privilégié, affirmer une feature CPU ou satisfaire un `requires`
sans règle de preuve enregistrée.

## 13. Macros AST hygiéniques

Une macro reçoit et retourne des nœuds AST typés; elle ne reçoit pas une chaîne à
réinjecter dans le parseur. L’hygiène applique une marque fraîche déterministe à
chaque symbole introduit. La capture d’un symbole du caller est interdite par
défaut et, si elle est ajoutée plus tard, devra employer une forme explicite et
auditable.

Les macros :

- utilisent le même évaluateur et les mêmes limites que comptime;
- n’effectuent aucun I/O;
- ne peuvent ni supprimer un effet/clobber réel ni fabriquer une preuve;
- sont développées avant vérification finale des types et contrats;
- conservent des spans de provenance macro → invocation → source;
- participent au hash de build avec leur AST, version et limites;
- ne peuvent émettre que des constructions permises par le target et le profil.

Une macro qui dépasse fuel, mémoire, nœuds ou profondeur échoue sans AST partiel
utilisable. L’ordre d’expansion est lexical et stable.

## 14. NTIR, Machine IR et convergence avec NX

NTIR, « Nexora Typed Instruction IR », est la représentation intermédiaire typée
proche machine commune aux chemins qui ont besoin de registres, effets, espaces
mémoire, contrats et intrinsics exacts. Ce n’est ni du texte NTASM ni un nouveau
backend. Machine IR porte ensuite les instructions sélectionnées, contraintes
d’encodage, blocs, symboles et relocations du target. La relation entre NTIR et
la NIR générale déjà prévue par l’architecture doit être figée dans leur schéma
versionné avant implémentation; aucun renommage implicite ne suffit.

```text
NTASM v0 → AST typé → HIR commune → NTIR ───────────────┐
                                                        ├→ Machine IR → encodeur commun
NX → AST/HIR commune → System IR → abaissement NTIR ────┘
```

NX n’a pas à générer du texte `.ntasm`; le frontend NTASM n’est pas réutilisé
comme parseur obligatoire du backend NX. La convergence se fait par structures
IR versionnées en mémoire ou dans un format Genesis canonique.

La HIR commune porte un noyau partagé et des nœuds spécifiques au profil; elle
ne rend pas les effets NTASM disponibles à NX. Réciproquement, NTASM ne
contourne pas la chaîne commune : résolution de
symboles, validation d’ABI, Machine IR, encodeur, writer NXO/PE et diagnostics
sont partagés lorsque leurs contrats sont identiques. Un intrinsic NTASM peut
abaisser directement vers NTIR/Machine IR sans équivalent textuel NX.

Les tests différentiels doivent prouver que des opérations sémantiquement
identiques issues de NX et NTASM donnent la même Machine IR pertinente et les
mêmes octets, symboles et relocations. Une différence autorisée doit être
motivée par un contrat ou une ABI explicite.

## 15. ABI, sections, symboles et relocations versionnés

Chaque artefact porte les versions exactes de ses contrats. Le writer refuse :

- ABI absente ou incompatible;
- section inconnue, permission W+X ou mapping non canonique;
- symbole dupliqué, import non typé ou export dont le contrat diffère;
- relocation non supportée par le target ou incompatible avec le type du site;
- valeur/addend hors plage ou ordre non canonique;
- champ réservé non nul, timestamp, chemin hôte ou overlay.

Une relocation est un record typé, pas un entier magique dans le frontend. Elle
nomme au minimum target, kind, largeur, section/site, symbole, addend et règle de
plage. Le backend choisit uniquement une relocation enregistrée par la version
du format. Les records sont triés par section, offset, kind et symbole selon un
ordre canonique documenté.

Les sections, symboles et relocations créés par macros ou générateurs de stubs
suivent exactement le même chemin de validation que ceux écrits directement.

## 16. Déterminisme, diagnostics et sécurité

- Aucune table de hachage à ordre observable dans une sortie.
- Aucun timestamp, identifiant aléatoire, adresse hôte ou chemin absolu.
- Arithmétique de tailles, offsets, alignements et addends checked.
- Aucun pointeur d’un espace plus privilégié obtenu par inférence implicite.
- Aucun intrinsic privilégié sans target, feature, effet et précondition.
- Aucune page ou section W+X; émission en mémoire RW puis scellement RX pour le
  JIT, jamais RWX.
- Premier diagnostic par span source canonique; codes et paramètres stables.
- Toute expansion/génération conserve sa provenance et son budget.
- Tout appel vérifie ABI, paramètres registre, stack, requires, ensures, effets
  et clobbers.
- Toute sortie binaire possède un inspecteur Genesis et des goldens indépendants
  du writer.

## 17. Critères avant implémentation de NTASM v0

Cette spécification conceptuelle autorise la conception, pas encore le codage du
frontend final. Avant le premier comportement v0, il faut figer :

1. grammaire lexicale/EBNF complète et récupération d’erreur;
2. catalogue v0 des types, espaces, registres, features et intrinsics;
3. logique fermée des prédicats `requires`/`ensures`;
4. règles exactes de composition des effets et clobbers;
5. ABI `nx64-abi-v0`, dont appels, frames, retours interruption/syscall;
6. schémas versionnés NTIR, Machine IR, sections, symboles et relocations;
7. syntaxe et plafonds numériques de comptime/macros;
8. déclarations structurées exactes des stubs;
9. corpus positif/négatif et goldens écrits avant l’implémentation;
10. gates de promotion séparés de B1/B2 et preuve qu’aucune feature v0 n’est
    entrée dans le seed S0.

Tant que ces points ne sont pas gelés, `spec/ntasm-s0.md` reste la seule
spécification exécutable du bootstrap et ce document ne change aucun attendu de
Gate B1.
