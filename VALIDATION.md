# Vérification du snapshot public

Ces commandes ciblent Linux x86_64 avec GCC et Bash (également utilisables sous WSL). Elles exécutent le code Nova généré par le bootstrap C de cette copie.

Depuis la racine du dépôt :

```bash
mkdir -p build
set -euo pipefail
root=bootstrap/quarantine/c
libs=("$root"/{nova,ntasm,frontend,codegen,x64,pe,object,object_build,object_link,object_materialize,conformance}.c "$root/nova/run_host.c" "$root/nova/runtime_v2.c")
for gate in effects contracts literal_initializers condition_types initializer_types constant_types validate nxo_link_pe; do
  gcc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$root" "${libs[@]}" "$root/nova/tests_ntasm_${gate}.c" -o "build/$gate"
  ASAN_OPTIONS=detect_leaks=1 "build/$gate" || exit 1
done
gcc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$root" "${libs[@]}" "$root/tests_nova_v2.c" -o build/nova_v2
ASAN_OPTIONS=detect_leaks=1 build/nova_v2
```

Rejoué le **19 septembre 2026** dans cette copie sous WSL/Linux x86_64, GCC avec AddressSanitizer, UndefinedBehaviorSanitizer et détection des fuites :

| Contrôle | Résultat |
|---|---|
| Vocabulaire des effets et espaces mémoire | 54 contrôles, 0 échec, 17 cas |
| Noms et arités des prédicats de contrats | 36 contrôles, 0 échec, 11 cas |
| Initialisations littérales | 39 contrôles, 0 échec, 12 cas |
| Conditions booléennes | 39 contrôles, 0 échec, 12 cas |
| Initialisations par noms et appels | 27 contrôles, 0 échec, 8 cas |
| Constantes scalaires | 33 contrôles, 0 échec, 10 cas |
| Pipeline sémantique NTASM en Nova (33 passes) | 213 contrôles, 0 échec, 70 cas |
| Deux objets NXO vers PE, comparaison binaire avec C | 35 contrôles, 0 échec, 5 cas |
| Nova v2, dont champs buffer et `slice` | 59 contrôles, 0 échec |

Le validateur couvre les cas présents dans son corpus, pas l'intégralité de la spécification. La comparaison NXO vers PE porte sur cinq cas et utilise l'implémentation C comme oracle. Le compilateur Nova dépend encore du bootstrap C : aucun auto-hébergement ni build complet de l'OS n'est revendiqué.

Les passes couvrent désormais une composition des effets/clobbers aux appels, des effets réels d'instructions et un sous-ensemble des preuves de contrats. Elles ne constituent pas une preuve complète de tous les prédicats ni un catalogue d'instructions exhaustif. Voir [la spécification](spec/ntasm.md) et [les critères de livraison](docs/ntasm-completion.md). L'inférence générale des expressions arithmétiques d'initialiseur reste ouverte.
