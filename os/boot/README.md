# Nexora OS — jalon Stage 0

`stage0.ntasm` est la première entrée UEFI du chemin OS écrite dans un langage
du projet. Le bootstrap C quarantainé de NTASM produit un PE32+ UEFI; le code
exécuté dans la VM provient uniquement de la source NTASM.

Le programme appelle réellement `SystemTable->ConOut->OutputString` au moyen de
la forme bornée `call_indirect GPR64` du bootstrap NTASM et affiche
`Hello World\r\n` sur la console UEFI. Le banc OVMF route cette console vers le
journal série, qui constitue la preuve fidèle. Le programme écrit aussi
`Hello World\n` sur le port debug QEMU `0xE9`, puis termine la VM via
`isa-debug-exit` sur `0xF4`. Ces deux transports restent réservés au test
QEMU/OVMF et ne doivent jamais être lancés sur du matériel réel.

Ce jalon prouve l'entrée UEFI et l'exécution d'instructions NTASM. Il ne quitte
pas encore les Boot Services, n'installe pas de noyau, de mémoire virtuelle ou
de pilotes et ne constitue pas un OS utilisable. Cnuva et NX restent hors de ce
chemin tant que leurs compilateurs ne sont pas prêts et prouvés.

Les artefacts, commandes et journaux reproductibles sont sous
`out/os-console-uefi-20260920/`.
