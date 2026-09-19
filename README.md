# Nexora Genesis

Chaîne NTASM/Nova pour produire des programmes exécutables.

**Nature :** Langages · recherche. Projet d’apprentissage et d’expérimentation réalisé avec l’aide d’IA. Cela ne signifie pas qu’une personne seule a écrit ou maîtrise l’ensemble du code.

## État de ce dépôt

**Snapshot de recherche, pas encore une toolchain complète livrable.** Cette copie de publication est mise à jour le **19 septembre 2026**, avec le pipeline NTASM en Nova de 33 passes et les modules validés des campagnes des 13 et 19 septembre. Elle ne contient pas l’historique Git des originaux.

Les deux catégories de preuve sont séparées : les anciens résultats concernent l’espace de travail original ; seuls les tests explicitement marqués « rejoué » ci-dessous concernent cette copie.

Binaires du compilateur, snapshots de campagne, images UEFI, journaux et recettes de publication privées sont absents. Aucun point fixe final ni NTASM v0 complet revendiqué.

## Parcours de lecture et exemple

Lire l’exemple return42, puis suivre l’analyse et la liaison dans le bootstrap C. Les familles de langages NTASM et Nova restent en construction.

| Fichier présent | Rôle ou repère réel |
|---|---|
| [examples/ntasm-v0/c-bootstrap/return42.ntasm](examples/ntasm-v0/c-bootstrap/return42.ntasm) | Symboles repérés : `bootstrap` |
| [bootstrap/quarantine/c/main.c](bootstrap/quarantine/c/main.c) | Source ou point d’entrée à examiner |
| [bootstrap/quarantine/c/frontend.c](bootstrap/quarantine/c/frontend.c) | Symboles repérés : `Allocation` |
| [bootstrap/quarantine/c/object_link.c](bootstrap/quarantine/c/object_link.c) | Source ou point d’entrée à examiner |

## Structure

Éléments de premier niveau : `bootstrap`, `examples`, `toolchain`.

La liste exhaustive des sources sélectionnées figure dans [PUBLICATION.json](PUBLICATION.json). Elle permet de vérifier ce qui est réellement distribué et les adaptations propres à cette copie.

## Modules désormais inclus

- Analyse des déclarations, références, appels et arité ; contrôle des noms, paramètres, variables et registres dans `toolchain/ntasm-nova/`.
- Validation sémantique commune en Nova : [validate.nova](toolchain/ntasm-nova/validate.nova), comprenant 33 passes et les alias physiques x86_64.
- Effets et clobbers déclarés, propagation aux appels, contrôles des accès mémoire et d'intrinsèques ; preuve aux appels d'un sous-ensemble des contrats, décrit dans la [spécification](spec/ntasm.md).
- Contrôle de flot terminal, ABI nx64, types des arguments et retours, espaces de pointeurs, cibles, versions et fonctionnalités CPU.
- Types des initialisations, constantes et conditions `if`. Les initialisations entières acceptent un niveau d'opération arithmétique ou binaire avec opérandes de type exact ; les expressions imbriquées non prises en charge restent refusées.
- Inspection des symboles et relocations NXO, fusion de deux objets et chaîne [NXO vers PE](toolchain/ntasm-nova/nxo_link_pe.nova).
- Correction du bootstrap Nova pour passer un champ buffer aux fonctions et à `slice`.
- Capacité du bootstrap Nova portée à 524 288 nœuds et 131 072 variables locales après reproduction de limites sur le pipeline composé ; la suite Nova v2 reste verte.

Ces composants ne constituent pas encore NTASM v0 complet. Restent notamment les effets et contrats complets, l'ABI, les macros/comptime, la couverture d'encodage, les stubs système et la reconstruction autonome.

## Prérequis

### Premier programme de démarrage

[Stage 0 en NTASM](os/boot/stage0.ntasm) écrit `Hello World` dans debugcon sous QEMU/OVMF, puis termine la VM de test. La preuve d'exécution a été obtenue dans l'espace de travail original, avec une image UEFI de 3 072 octets ; les sources publiées ont été vérifiées à l'identique. Ce programme ne fournit pas encore d'affichage dans la console UEFI ni de noyau. Voir [sa portée et ses limites](os/boot/README.md). Le périmètre PolyForm des compilateurs n'est pas étendu au répertoire `os/` par cet ajout.

Compilateur C pour le bootstrap ; les autres étapes dépendent des outils et versions de la chaîne originale.

## Commandes et vérification

Les contrôles ciblés sont décrits dans [VALIDATION.md](VALIDATION.md), avec les commandes GCC utilisables depuis la racine de cette copie. Ils construisent des exécutables de test hôte ; aucune VM ni intervention sur un disque réel n'est nécessaire. Une construction complète de l'OS et le point fixe final restent hors de cette preuve.

Les références relatives des sources JavaScript/HTML et les dépendances locales des manifestes Cargo ont fait l’objet de contrôles statiques ciblés, ainsi que la syntaxe Python et les manifestes JSON reconnus. Ce contrôle ne remplace ni un build intégral ni un parcours utilisateur.

## Ce qui a été exclu ou adapté

Conversations, notes de travail privées, identifiants, clés, modèles, profils de navigateur, journaux, images disque, assets et dépendances locales ne sont pas redistribués. Les chemins de profil personnel ont été remplacés par des chemins d’exemple uniquement dans la copie. Les sources originales restent inchangées. Les scripts de système ou de matériel doivent rester dans un environnement de laboratoire : ne pas les lancer sur un disque réel ou une machine de production.

## Aide de l’IA, composants tiers et droits

Les IA ont participé aux explications, à l’écriture et aux vérifications. Les langages, bibliothèques, moteurs et outils utilisés restent attribués à leurs auteurs. Une adaptation ou un prototype inspiré d’un univers tiers n’en revendique pas la création.

Les compilateurs couverts sont proposés sous [PolyForm Small Business 1.0.0](LICENSE), texte officiel inchangé. Le [périmètre de la licence](LICENSING.md) distingue les compilateurs des autres composants et précise la possibilité d'un accord commercial séparé. Les licences tierces restent applicables. Aucun taux automatique de redevance n'est ajouté à PolyForm.
