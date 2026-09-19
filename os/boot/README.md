# Nexora OS — jalon Stage 0

`stage0.ntasm` est la première entrée UEFI du chemin OS écrite dans un langage
du projet. Le bootstrap C quarantainé de NTASM produit un PE32+ UEFI; le code
exécuté dans la VM provient uniquement de la source NTASM.

Le programme écrit exactement `Hello World\n` sur le port debug QEMU `0xE9`,
puis termine la VM via `isa-debug-exit` sur `0xF4`. Ce transport est réservé au
test QEMU/OVMF et ne doit jamais être lancé sur du matériel réel.

La console UEFI visuelle reste hors de ce jalon : l’encodeur x64 sait produire
un `call` indirect, mais le frontend NTASM courant réserve `call` aux fonctions
résolues en REL32 et n’expose pas encore `ConOut->OutputString`. La trace
debugcon est donc la preuve fidèle retenue, sans prétendre à un affichage écran.

Ce jalon prouve l'entrée UEFI et l'exécution d'instructions NTASM. Il ne quitte
pas encore les Boot Services, n'installe pas de noyau, de mémoire virtuelle ou
de pilotes et ne constitue pas un OS utilisable. Cnuva et NX restent hors de ce
chemin tant que leurs compilateurs ne sont pas prêts et prouvés.

Les artefacts, commandes et journaux reproductibles sont sous
`out/os-hello-world-20260919/`.
