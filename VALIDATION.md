# Vérification du snapshot public

Rejoué le **26 septembre 2026** dans cette copie sous WSL/Linux x86_64, GCC, AddressSanitizer et UndefinedBehaviorSanitizer, avec détection des fuites. Les harnesses ci-dessous sont compilés avec `-Wall -Wextra -Werror`.

| Contrôle | Vérifications réussies |
|---|---:|
| Lexer / parser NTASM | 311 / 52 |
| Initialisations / retours / arguments / conditions | 96 / 87 / 78 / 150 |
| Pipeline global | 243 (80 cas) |
| Encodeur fixe / immédiats / mémoire / setcc | 225 / 8715 / 1284 / 792 |
| Appels locaux multiples : NXO / PE | 225 / 225 |
| Appel local avec données : NXO / PE | 52 / 55 |
| Objet combiné données/import | 42 |
| Fusion NXO / liaison PE | 84 / 35 |
| Nova v2 | 59 |
| Reader/writer NXO (fixture publiée) | 2490 |
| Appel indirect GPR64 | 4 |

## Reproduire les contrôles

Depuis la racine du dépôt, dans Bash avec GCC disponible :

```bash
set -euo pipefail
mkdir -p build
root=bootstrap/quarantine/c
libs=("$root"/{nova,ntasm,frontend,codegen,x64,pe,object,object_build,object_link,object_materialize,conformance}.c "$root/nova/run_host.c" "$root/nova/runtime_v2.c")
objects=()
for source in "${libs[@]}"; do
  object="build/$(basename "$source" .c).o"
  gcc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$root" -c "$source" -o "$object"
  objects+=("$object")
done
for gate in lexer parser initializer_types return_types call_types condition_types validate x64_fixed x64_ri x64_memory x64_setcc x64_lower_nxo_local_multi x64_lower_nxo_local_multi_pe x64_lower_nxo_local_data x64_lower_nxo_local_data_pe x64_lower_nxo_combined nxo_merge nxo_link_pe; do
  gcc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$root" "${objects[@]}" "$root/nova/tests_ntasm_${gate}.c" -o "build/$gate"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "build/$gate"
done
gcc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$root" "${objects[@]}" "$root/tests_nova_v2.c" -o build/nova_v2
ASAN_OPTIONS=detect_leaks=1 build/nova_v2
```

## Portée et limites

Ces résultats portent sur les corpus fournis, pas sur toute la spécification. Les NXO/PE sont comparés octet par octet aux oracles C. Les API d'abaissement restent des profils bornés : noms et formes contrôlés, appels multiples vers le helper local, données scalaires émises mais non référencées par le code.

`&&` et `||` ont une preuve lexicale, syntaxique et de typage ; aucun court-circuit x64 à l'exécution n'est encore revendiqué. Les expressions constantes ne sont pas encore évaluées et transmises aux octets de données.

Le compilateur Nova dépend encore du bootstrap C. Aucun auto-hébergement, NTASM B2.1 complet ou build complet de l'OS n'est revendiqué. Voir [la spécification](spec/ntasm.md) et [les critères de livraison](docs/ntasm-completion.md).

La preuve UEFI du Hello World du 20 septembre provient de l'espace de travail original ; aucun nouveau boot QEMU n'est revendiqué pour cette publication.

## Adaptations de publication

Deux dépendances de test auparavant sous `out/` sont distribuées sous `toolchain/ntasm-nova/tests/fixtures/` : le témoin NTASM d'exception et le module PE de dépendance du banc NXO. Seuls les chemins des harnesses ont été adaptés dans cette copie. Aucune note privée, clé, configuration d'agent ou image disque n'est incluse.
