# Nexora Kart

Un jeu de kart façon Mode 7 qui démarre comme son propre petit OS. Tout ce
qui s'exécute sur la machine, noyau compris, est écrit en **NTASM** et
assemblé par l'assembleur NTASM du dépôt. Aucune ligne de C ne tourne dans la
VM. Graphismes, circuit et police sont originaux et générés par script.

## Jouer

Il faut Linux ou WSL avec `gcc`, `python3`, `qemu-system-x86` et `ovmf` :

```bash
sudo apt install gcc python3 qemu-system-x86 ovmf
os/kart/build.sh   # compile l'assembleur NTASM, les assets puis le jeu
os/kart/run.sh     # ouvre une fenêtre QEMU/UEFI
```

| Touche | Action |
|---|---|
| ← → ou Q D (A D en QWERTY) | tourner |
| ↑ ou Z (W en QWERTY) | accélérer |
| ↓, S ou Espace | freiner, reculer |
| Entrée | démarrer, revenir au menu |
| Échap | quitter (le noyau redémarre la machine, QEMU se ferme) |

`SOUND=1 os/kart/run.sh` envoie le haut-parleur PC (bips du départ) vers
PulseAudio. `QEMU_EXTRA="-display gtk,zoom-to-fit=on"` agrandit la fenêtre.

## Ce qui se passe au démarrage

1. **Firmware UEFI** : `efi_main` (dans `src/kernel.ntasm`) choisit le mode
   graphique 640×480, réserve 8 Mio avec `AllocatePages` et mesure la
   fréquence du TSC contre `Stall(50 ms)`.
2. **`ExitBootServices`** : après `GetMemoryMap`, le firmware disparaît et
   les interruptions sont coupées. À partir de là, le noyau pilote lui-même
   le framebuffer, le contrôleur clavier PS/2, le haut-parleur PC et le port
   série COM1.
3. **Le jeu** avance 60 fois par seconde au pas fixe, avec le temps mesuré au
   TSC. Chaque tour de boucle dessine une image 320×200 en 256 couleurs,
   agrandie ×2 à l'écran.

Le port série affiche `NEXORA OS (NTASM): boot…` puis
`NEXORA: firmware exited, the machine is ours`.

## Organisation

| Fichier | Rôle |
|---|---|
| `src/kernel.ntasm` | démarrage UEFI, sortie du firmware, clavier, horloge, affichage, son, division logicielle |
| `src/gfx.ntasm` | ciel et montagnes en parallaxe, sol Mode 7, sprites agrandis, texte 5×7 |
| `src/race.ntasm` | conduite (herbe, sable, boosts), pilotes ordinateur, tours, classement, caméra, mini-carte |
| `src/game.ntasm` | écrans titre, départ, course et arrivée ; boucle principale |
| `src/layout.inc` | carte mémoire et réglages |
| `tools/gen_assets.py` | génère `build/assets.ntasm` : palette, circuit 512×512, sprites, police, tables sinus/inverse/perspective, points de passage |
| `tools/ntpp.py` | remplace les constantes nommées (`%define`) : NTASM n'accepte que des entiers dans les déplacements mémoire |
| `tools/qemu_capture.py` | lance le jeu sans écran, envoie des touches et prend des captures (tests) |

`AUTOPILOT=1 DEBUG=1 os/kart/build.sh` produit une version de test : un
pilote ordinateur conduit le kart du joueur et COM1 affiche le nombre
d'images et la progression.

## Extensions ajoutées à l'assembleur NTASM

Le vérificateur de `bootstrap/quarantine/c` ne laisse fabriquer aucun
pointeur à partir d'un entier. Il n'avait pas non plus de moyen d'appeler une
fonction UEFI à plus de quatre arguments. Deux instructions explicites ont
été ajoutées :

- `adopt r64,[adresse]:ptr<espace,T>` calcule l'adresse comme `lea` et donne
  au registre le type pointeur déclaré, avec droit d'écriture. C'est la seule
  conversion entier → pointeur. Elle impose l'effet `privileged`, sinon
  l'erreur E501 s'affiche. Elle sert pour le framebuffer, les tables du
  firmware et les vues typées des tableaux d'octets.
- `call_efi cible,arg5,…` appelle une fonction UEFI. Les arguments 1 à 4 sont
  déjà dans `rcx`, `rdx`, `r8`, `r9`, et les registres listés sont posés sur
  la pile au-dessus de la zone d'ombre, que la frame réserve.

Les suites de validation publiées restent vertes. `tests_conformance_permissions`
signale un échec, mais il existe à l'identique sur le code d'origine.

## Limites connues

- Ce n'est pas un OS général : pas d'interruptions, de processus, de
  pagination propre ni de système de fichiers.
- Testé uniquement sous QEMU/OVMF (q35). Sur un vrai PC, le clavier USB n'est
  plus émulé en PS/2 après `ExitBootServices`. Ne pas lancer sur une machine
  contenant des données.
- Un seul circuit ; les karts sont toujours vus de dos.
- L'assembleur NTASM plante sur `mov r64,-1` (constante négative non typée
  dans une instruction générique). Le code utilise `0xffffffffffffffff` à la
  place. Ce bug existait déjà.
- `udiv` remplace `div`, que NTASM refuse faute de preuve que le diviseur est
  non nul.
