# Nexora OS

Un petit système d'exploitation **préemptif** dont le noyau est écrit en
**NTASM** et qui fait tourner des **programmes compilés en Nova** comme des
processus. Les deux langages sont ceux du dépôt. QEMU/OVMF uniquement.

C'est le grand frère du jeu `os/kart` : là où le kart est un seul programme
bare-metal, Nexora OS est un vrai noyau avec interruptions, pagination,
multitâche et un shell.

## Lancer

```bash
sudo apt install gcc python3 qemu-system-x86 ovmf
os/nexoros/build.sh
os/nexoros/run.sh
```

Une fenêtre QEMU s'ouvre. Clique dedans pour donner le clavier, puis tape des
commandes après l'invite `nexora>` :

| Commande | Effet |
|---|---|
| `help` | liste les commandes |
| `ps` | liste les tâches (état, nom, temps CPU) |
| `mem` | mémoire physique libre |
| `uptime` | temps depuis le démarrage |
| `spin` | crée une nouvelle tâche animée |
| `nova` | charge et exécute le programme Nova embarqué |
| `crash` | crée une tâche qui déborde sa pile (voir plus bas) |
| `clavier` | bascule AZERTY / QWERTY |
| `echo <texte>` | réaffiche le texte |
| `clear` | efface l'écran |
| `reboot` | redémarre |

Trois tâches « spinner » tournent en fond et continuent d'animer pendant que
tu tapes : c'est l'ordonnanceur préemptif au travail.

## Ce que fait le noyau

1. **Démarrage UEFI** : choix d'un mode 640×480, 64 Mio pris au firmware,
   calibration du TSC, puis `ExitBootServices`. Ensuite le noyau est seul.
2. **Console** : grille 80×30 rendue au framebuffer avec une police 8×16
   générée ; ligne 0 = barre d'état, le reste défile.
3. **Interruptions** : GDT et IDT maison (l'IDT est bâtie par la déclaration
   `interrupt_table` de NTASM). PIC 8259 remappé, PIT à 1 kHz sur IRQ0,
   clavier PS/2 sur IRQ1 (files de caractères, AZERTY/QWERTY).
4. **Pagination** : le noyau installe ses propres tables (identité 4 Gio en
   pages de 2 Mio) avec une fenêtre de 2 Mio en pages de 4 Kio pour les piles
   de tâches, chacune précédée d'une **page de garde** non mappée.
5. **Ordonnanceur préemptif** : à chaque tic du timer, le gestionnaire
   sauvegarde la frame de registres de la tâche courante et renvoie celle
   d'une autre tâche prête ; l'`iretq` reprend donc une tâche différente.
   `sleep`, `exit` et la mort d'une tâche sont gérés.
6. **Protection de pile** : un TSS avec une pile d'interruption dédiée (IST)
   pour `#PF`/`#DF`. Quand une tâche déborde sa pile et touche sa page de
   garde, la faute est traitée sur la pile IST : la tâche est **tuée** et les
   autres continuent, au lieu d'un plantage noyau. `crash` le démontre.
7. **Processus Nova** : `nova` charge un programme compilé par le compilateur
   Nova du dépôt, avec la disposition mémoire que ce compilateur attend
   (texte, puis rdata après un ancrage de 8 octets à la page suivante, puis
   data), crée un contexte ABI2 minimal et appelle son entrée. Le résultat
   (une boucle `while` qui calcule 42) est affiché.

## Organisation

| Fichier | Rôle |
|---|---|
| `src/nexoros.ntasm` | module racine, données globales, ordre des `%include` |
| `src/boot.ntasm` | démarrage UEFI, sortie du firmware, série, division |
| `src/console.ntasm` | console texte et rendu au framebuffer |
| `src/core.ntasm` | GDT, IDT (`interrupt_table`), PIC, PIT, clavier, handler |
| `src/memory.ntasm` | pagination avec pages de garde, TSS/IST, scan RAM |
| `src/sched.ntasm` | tâches, `schedule`, `sleep`, `exit` |
| `src/panic.ntasm` | écran de panique (frame fautive) |
| `src/demo.ntasm` | tâches spinner de démonstration |
| `src/shell.ntasm` | shell interactif (tâche 0) et commandes |
| `src/nova.ntasm` | chargeur et exécution des programmes Nova |
| `apps/hello.nova` | le programme Nova embarqué |
| `tools/gen_assets.py` | palette, police 8×16, tables clavier, chaînes |
| `tools/embed_nova.py` | extrait les sections d'un PE Nova en NTASM |
| `tools/ntpp.py` | remplace les constantes nommées avant l'assemblage |

## Extensions ajoutées au compilateur NTASM

Pour écrire un noyau à interruptions, deux formes ont été ajoutées au
compilateur `bootstrap/quarantine/c` (voir aussi `os/kart` pour `adopt` et
`call_efi`) :

- `interrupt_table NAME { handler FN  vectors A B }` génère un stub d'entrée
  de 16 octets par vecteur (poussant un code d'erreur synthétique là où le
  CPU n'en pousse pas, puis le vecteur) et un répartiteur commun qui
  sauvegarde les registres, appelle `FN(in frame:u64 @rcx) -> u64` et reprend
  sur la frame renvoyée — renvoyer une autre frame change donc de tâche.
- `fn_address r64,nom` charge l'adresse d'une fonction locale (pour installer
  la table dans l'IDT).

Les suites de validation publiées du dépôt restent vertes après ces ajouts
(le seul échec, `tests_conformance_permissions`, préexistait).

## Limites connues

- QEMU/OVMF (q35) uniquement. Pas de vrai matériel : après `ExitBootServices`
  un clavier USB réel ne répond plus en PS/2.
- L'« uptime » avance plus vite que le temps réel : en émulation pure (sans
  KVM) QEMU accélère le temps virtuel pendant les `hlt`. Correct sous KVM.
- Le runtime Nova implémenté couvre un programme de **calcul** (aucun callback
  d'allocation ou d'entrée-sortie). Les callbacks ABI2 complets (alloc, diag,
  fichiers) et donc les programmes Nova qui écrivent eux-mêmes à l'écran
  restent à faire ; le chargeur et l'appel d'entrée, eux, fonctionnent.
- Pas de système de fichiers ni de mémoire virtuelle par processus.
- Un seul cœur ; l'ordonnancement est un tourniquet simple.
