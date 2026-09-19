# Seed PE32+ x86_64 UEFI — profils B0 et B1

Statut : spécification normative initiale, version `S0.1`.

Ce document fige deux enveloppes distinctes : le Hello manuel `B0-PE-v0` et le
format producteur `B1-PE-v1` émis par NTASM-S0. Il n'autorise aucun linker,
générateur PE ou import externe dans la chaîne finale. L'ADR 0009 autorise
temporairement NASM `-f bin` uniquement pour des candidats B1.x quarantainés,
jamais pour une sortie finale. Tous les entiers multioctets sont
little-endian. **DOIT** exprime une exigence; toute valeur réservée ou non
mentionnée est nulle.

Le seed initial reste inférieur ou égal à `65 536` octets et chacun de ses
octets doit être expliqué dans `bootstrap/seed/seed0-bytes.md` avant la Gate B0.
`B0-PE-v0` était provisoire jusqu'au test OVMF. Le fichier exact du ledger a été
chargé deux fois avec succès le 2 septembre 2026; son absence de relocation est
donc validée pour ce banc précis, sans généralisation aux firmwares physiques.
`B1-PE-v1` est le format canonique futur, indépendant de cette concession
minimale et obligatoirement relogeable par `.reloc`.

## 1. Deux profils fermés

| Propriété | `B0-PE-v0` Hello validé OVMF | `B1-PE-v1` producteur canonique |
|---|---|---|
| PE / machine / sous-système | PE32+ / `0x8664` / EFI Application 10 | identique |
| offset PE | `0x0040` | `0x0080` |
| taille en-têtes | `0x0200` | `0x0400` |
| base préférée | `0` | `0x0000000140000000` |
| sections | `.text`, `.rdata` | `.text`, `.rdata`, `.data`, `.reloc` |
| relocation | aucune, code entièrement RIP-relatif | `DIR64` non vide obligatoire |
| imports | aucun | aucun |
| usage | preuve `NG_SEED0_OK` seulement | toute sortie de NTASM-S0 |
| autorité octet | `bootstrap/seed/seed0-bytes.md` | golden de ce document et `ntasm-s0.md` |

Les deux profils utilisent `SectionAlignment = 0x1000`,
`FileAlignment = 0x0200`, timestamp et checksum nuls, et interdisent tout
overlay. Il n'existe aucune troisième variante implicite.

Les images intermédiaires `B1.x` du bootstrap peuvent utiliser exactement
l'enveloppe `B1-PE-v1` avant de savoir produire elles-mêmes un PE. Elles restent
soit des candidats saisis depuis un ledger, soit des candidats NASM externes
explicitement quarantainés par l'ADR 0009. Ce ne sont pas des sorties de NTASM
et elles ne valident jamais la Gate B1 par leur seule exécution.

### 1.1 `B0-PE-v0` — image Hello exacte validée sur le banc B0

Le fichier B0 fait exactement `0x600` octets et son SHA-256 attendu est
`69fa9aedbe1d009457c6cd065a78d90d1d997111d49719ce79fe3045603901d0` :

```text
0x0000..0x003F  DOS minimal; e_lfanew = 0x40
0x0040..0x0147  signature PE, COFF et Optional Header 0xF0
0x0148..0x0197  deux en-têtes de section
0x0198..0x01FF  padding nul des en-têtes
0x0200..0x03FF  .text, raw size 0x200
0x0400..0x05FF  .rdata, raw size 0x200
```

Champs B0 qui diffèrent de B1 :

| Champ | Offset | Valeur B0 |
|---|---:|---:|
| `e_lfanew` | `0x003C` | `0x00000040` |
| signature | `0x0040` | `50 45 00 00` |
| `Machine` / `NumberOfSections` | `0x0044` / `0x0046` | `0x8664` / `2` |
| `SizeOfOptionalHeader` / COFF chars | `0x0054` / `0x0056` | `0x00F0` / `0x0022` |
| PE32+ magic | `0x0058` | `0x020B` |
| `SizeOfCode` / `SizeOfInitializedData` | `0x005C` / `0x0060` | `0x200` / `0x200` |
| entry / base of code | `0x0068` / `0x006C` | `0x1000` / `0x1000` |
| `ImageBase` | `0x0070` | `0` |
| section / file align | `0x0078` / `0x007C` | `0x1000` / `0x0200` |
| `SizeOfImage` / `SizeOfHeaders` | `0x0090` / `0x0094` | `0x3000` / `0x0200` |
| subsystem / DLL chars | `0x009C` / `0x009E` | `10` / `0x0100` (`NX_COMPAT`) |
| stack/heap reserve et commit | `0x00A0..0x00BF` | zéro |
| `NumberOfRvaAndSizes` | `0x00C4` | `16` |
| 16 data directories | `0x00C8..0x0147` | toutes nulles |

`.text` a `VirtualSize=0x1C`, RVA `0x1000`, raw pointer `0x0200`, raw
size `0x0200` et caractéristiques `0x60000020`. `.rdata` a
`VirtualSize=0x1C`, RVA `0x2000`, raw pointer `0x0400`, raw size `0x0200` et
caractéristiques `0x40000040`. Les autres champs des deux section headers sont
nuls. Le code utile, la chaîne CHAR16, le padding et le hash exacts restent
définis par le ledger B0.

L'absence de `.reloc` est une hypothèse OVMF étroite, pas le précédent du writer.
Elle est acceptable **si et seulement si** le test B0 exact réussit deux fois.
Si OVMF refuse l'image, B0 reste rouge et ce profil doit être révisé avec les
preuves d'échec; il est interdit de déclarer le cas validé par raisonnement seul.

### 1.2 `B1-PE-v1` — calcul canonique du writer

Une image B1 a toujours quatre sections non vides. Leurs adresses sont calculées;
elles ne sont pas choisies par l'implémentation.

```text
headers.raw = 0x0400
.text.rva   = 0x1000
.text.raw   = 0x0400

next_rva(s) = align_up(s.rva + max(s.virtual_size, s.raw_size), 0x1000)
next_raw(s) = s.raw_pointer + s.raw_size
raw_size(s) = align_up(s.virtual_size, 0x0200)

.rdata.rva  = next_rva(.text)
.rdata.raw  = next_raw(.text)
.data.rva   = next_rva(.rdata)
.data.raw   = next_raw(.rdata)
.reloc.rva  = next_rva(.data)
.reloc.raw  = next_raw(.data)

SizeOfImage = align_up(.reloc.rva +
                       max(.reloc.virtual_size, .reloc.raw_size), 0x1000)
file_size   = .reloc.raw_pointer + .reloc.raw_size
```

`virtual_size` est le nombre exact d'octets utiles. Le padding jusqu'à
`raw_size` vaut zéro, y compris après le code; le `90` n'est utilisé que par une
directive NTASM `align` interne à `.text`. Le fichier se termine au dernier octet
du padding `.reloc`; tout octet supplémentaire serait un overlay interdit.

## 2. B1 — en-tête DOS et zone jusqu'au PE

La zone `0x0000..0x007F` fait exactement 128 octets.

| Offset | Taille | Champ | Valeur |
|---:|---:|---|---:|
| `0x0000` | 2 | `e_magic` | `4D 5A` (`MZ`) |
| `0x0002` | 58 | autres champs DOS | zéro |
| `0x003C` | 4 | `e_lfanew` | `0x00000080` |
| `0x0040` | 64 | stub/réserve | zéro; aucun message DOS |

Il n'existe ni Rich header, ni programme DOS, ni donnée cachée dans cette zone.

## 3. B1 — signature et en-tête COFF

| Offset | Taille | Champ | Valeur |
|---:|---:|---|---:|
| `0x0080` | 4 | signature | `50 45 00 00` |
| `0x0084` | 2 | `Machine` | `0x8664` |
| `0x0086` | 2 | `NumberOfSections` | `4` |
| `0x0088` | 4 | `TimeDateStamp` | `0` |
| `0x008C` | 4 | `PointerToSymbolTable` | `0` |
| `0x0090` | 4 | `NumberOfSymbols` | `0` |
| `0x0094` | 2 | `SizeOfOptionalHeader` | `0x00F0` |
| `0x0096` | 2 | `Characteristics` | `0x0022` |

`Characteristics` contient uniquement `IMAGE_FILE_EXECUTABLE_IMAGE` (`0x0002`)
et `IMAGE_FILE_LARGE_ADDRESS_AWARE` (`0x0020`). Le bit
`IMAGE_FILE_RELOCS_STRIPPED` est interdit.

Golden fixe de `0x0080..0x0097` :

```text
50 45 00 00 64 86 04 00 00 00 00 00 00 00 00 00
00 00 00 00 F0 00 22 00
```

## 4. B1 — Optional Header PE32+

L'Optional Header occupe `0x0098..0x0187`, soit `0xF0` octets.

| Offset | Taille | Champ | Valeur ou calcul |
|---:|---:|---|---|
| `0x0098` | 2 | `Magic` | `0x020B` |
| `0x009A` | 1 | `MajorLinkerVersion` | `0` |
| `0x009B` | 1 | `MinorLinkerVersion` | `0` |
| `0x009C` | 4 | `SizeOfCode` | `.text.raw_size` |
| `0x00A0` | 4 | `SizeOfInitializedData` | somme des raw sizes `.rdata + .data + .reloc` |
| `0x00A4` | 4 | `SizeOfUninitializedData` | `0` |
| `0x00A8` | 4 | `AddressOfEntryPoint` | RVA du label `entry` dans `.text` |
| `0x00AC` | 4 | `BaseOfCode` | `.text.rva` (`0x1000`) |
| `0x00B0` | 8 | `ImageBase` | `0x0000000140000000` |
| `0x00B8` | 4 | `SectionAlignment` | `0x1000` |
| `0x00BC` | 4 | `FileAlignment` | `0x0200` |
| `0x00C0` | 2 | `MajorOperatingSystemVersion` | `0` |
| `0x00C2` | 2 | `MinorOperatingSystemVersion` | `0` |
| `0x00C4` | 2 | `MajorImageVersion` | `0` |
| `0x00C6` | 2 | `MinorImageVersion` | `0` |
| `0x00C8` | 2 | `MajorSubsystemVersion` | `0` |
| `0x00CA` | 2 | `MinorSubsystemVersion` | `0` |
| `0x00CC` | 4 | `Win32VersionValue` | `0` |
| `0x00D0` | 4 | `SizeOfImage` | formule de la section 1 |
| `0x00D4` | 4 | `SizeOfHeaders` | `0x0400` |
| `0x00D8` | 4 | `CheckSum` | `0` |
| `0x00DC` | 2 | `Subsystem` | `0x000A` (`EFI_APPLICATION`) |
| `0x00DE` | 2 | `DllCharacteristics` | `0x0140` |
| `0x00E0` | 8 | `SizeOfStackReserve` | `0x00100000` |
| `0x00E8` | 8 | `SizeOfStackCommit` | `0x00001000` |
| `0x00F0` | 8 | `SizeOfHeapReserve` | `0x00100000` |
| `0x00F8` | 8 | `SizeOfHeapCommit` | `0x00001000` |
| `0x0100` | 4 | `LoaderFlags` | `0` |
| `0x0104` | 4 | `NumberOfRvaAndSizes` | `16` |

`DllCharacteristics = IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE (0x0040) |
IMAGE_DLLCHARACTERISTICS_NX_COMPAT (0x0100)`. L'image ne dépend jamais de sa base
préférée.

### 4.1 Répertoires de données

Les 16 entrées de huit octets commencent à `0x0108`. Toutes sont nulles sauf
l'index 5, Base Relocation Table :

| Index | Offset | RVA | Size |
|---:|---:|---|---|
| 0 Export | `0x0108` | `0` | `0` |
| 1 Import | `0x0110` | `0` | `0` |
| 3 Exception | `0x0120` | `0` | `0` |
| 5 Base Relocation | `0x0130` | `.reloc.rva` | `.reloc.virtual_size` |
| 4 Security et tous les autres | selon index | `0` | `0` |

Il n'existe donc ni import, export, certificat, ressource, unwind `.pdata`, TLS,
debug, IAT ou délai d'import. Le répertoire Security, dont le premier champ est un
offset fichier plutôt qu'un RVA, reste lui aussi entièrement nul.

## 5. B1 — table des sections

La table commence à `0x0188`. Chaque entrée mesure 40 octets. Elle finit à
`0x0227`; `0x0228..0x03FF` vaut zéro.

| Entrée | Offset | Nom (8 octets) | `VirtualSize` | `VirtualAddress` | `SizeOfRawData` | `PointerToRawData` | Characteristics |
|---:|---:|---|---|---|---|---|---:|
| 0 | `0x0188` | `.text\0\0\0` | utile `.text` | calculé | aligné `0x200` | calculé | `0x60000020` |
| 1 | `0x01B0` | `.rdata\0\0` | utile `.rdata` | calculé | aligné `0x200` | calculé | `0x40000040` |
| 2 | `0x01D8` | `.data\0\0\0` | utile `.data` | calculé | aligné `0x200` | calculé | `0xC0000040` |
| 3 | `0x0200` | `.reloc\0\0` | taille des blocs | calculé | aligné `0x200` | calculé | `0x42000040` |

Tous les champs `PointerToRelocations`, `PointerToLinenumbers`,
`NumberOfRelocations` et `NumberOfLinenumbers` sont nuls.

Signification des caractéristiques :

- `.text` : code, lecture, exécution; jamais écriture;
- `.rdata` : données initialisées, lecture seule, non exécutables;
- `.data` : données initialisées, lecture/écriture, non exécutables;
- `.reloc` : données initialisées, lecture seule, discardable, non exécutables.

Aucune section n'est W+X. Les permissions dérivées des sections sont le contrat
minimal même si un firmware particulier mappe temporairement l'image avec des
permissions plus larges.

## 6. B1 — relocations de base

NTASM-S0 ne crée une relocation que pour un `qword addr(label)` placé dans
`.rdata` ou `.data`. L'octet stocké avant relocation vaut :

```text
ImageBase + RVA(label)
```

La RVA du qword à corriger est groupée par page de 4 Kio. Pour chaque page, le
writer émet :

```text
u32 PageRVA                 ; RVA alignée vers le bas sur 0x1000
u32 BlockSize               ; 8 + 2 * nombre_d_entrees, multiple de 4
u16 entries[]               ; (10 << 12) | offset_dans_page
u16 0                       ; seulement si nécessaire pour aligner le bloc à 4
```

Le type 10 est `IMAGE_REL_BASED_DIR64`; l'éventuelle entrée nulle est
`IMAGE_REL_BASED_ABSOLUTE` et ne corrige rien. Les pages sont croissantes, puis
les offsets croissants. Les doublons sont interdits. Il n'y a ni bloc terminal,
ni donnée après le dernier bloc utile, hormis le padding brut nul de section.

Le répertoire de relocation doit être non vide. Une image minimale place donc
explicitement une ancre `qword addr(entry_label)` dans `.rdata`, même si le code
n'utilise pas cette valeur. Cette ancre rend la relocation observable et évite
de prétendre que l'image dépend d'une adresse de chargement fixe.

Golden pour une ancre à la RVA `0x2000` :

```text
00 20 00 00 0C 00 00 00 00 A0 00 00
```

## 7. Stratégie sans imports

Le seed n'a aucune import table. UEFI lui transmet ses capacités à l'entrée : le
premier argument est le handle de l'image, le second un pointeur vers la table
système, et le résultat est un statut EFI. Sur x64, `rcx` contient
`ImageHandle`, `rdx` contient `SystemTable`. Le
seed conserve ces deux valeurs dans des registres non volatils ou sur sa pile,
puis atteint les services uniquement par les pointeurs de protocoles. Aucune
adresse de firmware n'est inscrite dans le PE.

### 7.1 Offsets UEFI utilisés par la preuve B0

Les offsets suivants sont mesurés depuis le début de la structure indiquée et
sont figés pour le profil UEFI x64 :

| Structure | Membre | Offset |
|---|---|---:|
| `EFI_SYSTEM_TABLE` | `ConOut` | `0x40` |
| `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` | `OutputString` | `0x08` |

`OutputString` est appelé avec `rcx = ConOut` et `rdx = pointeur UTF-16`. Le B0
exact retourne ensuite `EFI_SUCCESS` au chargeur par `ret`; il ne déréférence pas
`BootServices` et n'appelle pas `Exit`. Cette différence est volontairement
limitée au Hello provisoire.

### 7.2 Offsets nécessaires au seed assembleur B1

Le seed B1 utilise les mêmes principes, plus les membres suivants :

| Structure | Membre | Offset |
|---|---|---:|
| `EFI_SYSTEM_TABLE` | `BootServices` | `0x60` |
| `EFI_BOOT_SERVICES` | `AllocatePool` | `0x40` |
| `EFI_BOOT_SERVICES` | `FreePool` | `0x48` |
| `EFI_BOOT_SERVICES` | `HandleProtocol` | `0x98` |
| `EFI_BOOT_SERVICES` | `Exit` | `0xD8` |
| `EFI_LOADED_IMAGE_PROTOCOL` | `DeviceHandle` | `0x18` |
| `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` | `OpenVolume` | `0x08` |
| `EFI_FILE_PROTOCOL` | `Open` | `0x08` |
| `EFI_FILE_PROTOCOL` | `Close` | `0x10` |
| `EFI_FILE_PROTOCOL` | `Read` | `0x20` |
| `EFI_FILE_PROTOCOL` | `Write` | `0x28` |
| `EFI_FILE_PROTOCOL` | `GetInfo` | `0x40` |
| `EFI_FILE_PROTOCOL` | `Flush` | `0x50` |

Les GUID en mémoire sont les 16 octets exacts suivants :

```text
LoadedImageProtocol:     A1 31 1B 5B 62 95 D2 11 8E 3F 00 A0 C9 69 72 3B
SimpleFileSystemProtocol:22 5B 4E 96 59 64 D2 11 8E 39 00 A0 C9 69 72 3B
EFI_FILE_INFO_ID:        92 6E 57 09 3F 6D D2 11 8E 39 00 A0 C9 69 72 3B
```

Le seed obtient `LoadedImageProtocol` via `HandleProtocol(ImageHandle, ...)`,
prend son `DeviceHandle`, obtient `SimpleFileSystemProtocol` sur ce handle, puis
appelle `OpenVolume`. Il utilise `EfiLoaderData = 2` pour `AllocatePool`.

Les trois payloads runtime ont les tailles fixes suivantes : `WORK=0x1D000`,
`SRC=0x10000`, `OUT=0x10000`. Pour placer une garde de 16 octets de chaque côté,
les tailles réellement passées à `AllocatePool` sont respectivement
`0x1D020`, `0x10020`, `0x10020`, soit `0x3D060` octets bruts au total. Le
pointeur utilisable vaut toujours `raw + 0x10`; les méthodes internes ne voient
que ce pointeur intérieur et la taille du payload. `FreePool` reçoit uniquement
le pointeur `raw`. Les six bandes de garde sont vérifiées avant libération; une
corruption produit `E603` avec position source nulle.

Constantes de fichier :

```text
EFI_FILE_MODE_READ   = 0x0000000000000001
EFI_FILE_MODE_WRITE  = 0x0000000000000002
EFI_FILE_MODE_CREATE = 0x8000000000000000
```

Après ouverture de la source, le seed appelle `GetInfo` avec
`EFI_FILE_INFO_ID`. Dans `EFI_FILE_INFO`, `Attribute` est un qword à l’offset
`0x48`; `EFI_FILE_DIRECTORY = 0x10`. Un buffer trop petit, un compte incohérent,
une erreur `GetInfo` ou le bit répertoire produit l’erreur de lecture E002 avant
tout appel à `Read`. `FileSize` n’autorise jamais la source à lui seul.

La source est ouverte avec `READ`, puis lue en boucle jusqu'à EOF ou jusqu'à ce
que le compteur atteigne la sentinelle `65 536`; atteindre cette sentinelle est
un refus de taille. Après un cumul de `65 535`, un appel `Read` final de capacité
`1` est obligatoire : zéro confirme EOF et accepte la source, un octet atteint
la sentinelle et la refuse. Cette lecture réelle distingue `65 535` de
`65 536`, indépendamment de `EFI_FILE_INFO.FileSize`.

Pour vérifier la sortie, le seed tente d'abord un `Open` en
`READ` : succès signifie « existe » et provoque le refus; seul `EFI_NOT_FOUND`
autorise la création, tout autre statut est une erreur I/O. La sortie est ensuite
ouverte avec `READ | WRITE | CREATE` et attributs zéro. Le seed garde l'image
produite en mémoire jusqu'à validation complète; il ne fait aucun seek, rename,
delete ou `SetInfo` dans S0.

Après un succès ou une erreur B1, `Exit` est appelé avec `rcx = ImageHandle`,
`rdx = ExitStatus`, `r8 = 0` et `r9 = 0`. Si `Exit` rend la main, son propre
statut devient la valeur de retour de l'application.

## 8. ABI EFI x64 et discipline d'appel

L'ABI est le Microsoft x64 utilisé par `EFIAPI` :

- les quatre premiers arguments entiers/pointeurs sont `rcx`, `rdx`, `r8`, `r9`;
- la valeur de retour est dans `rax`;
- `rax`, `rcx`, `rdx`, `r8..r11` sont volatils;
- `rbx`, `rbp`, `rdi`, `rsi`, `r12..r15` et `rsp` sont non volatils;
- le caller réserve 32 octets de shadow space pour chaque appel;
- immédiatement avant `call`, `rsp` est aligné sur 16 octets;
- au point d'entrée après l'appel du firmware, `rsp mod 16 = 8`;
- aucune red zone n'est supposée;
- le seed n'utilise ni flottants, ni SIMD, ni unwind, ni exception firmware.

Chaque chemin de retour restaure la pile et les registres non volatils qu'il a
modifiés. Une fonction UEFI peut écraser tous les registres volatils, même si une
version donnée d'OVMF semble les préserver.

## 9. Comportements de sortie

### 9.1 B0 Hello provisoire

Le fichier exact du ledger effectue uniquement ce flux :

1. réserver `0x28` octets de pile, ce qui couvre le shadow space et l'alignement;
2. charger `SystemTable->ConOut` depuis `[rdx + 0x40]`;
3. charger par RIP l'adresse de `L"NG_SEED0_OK\r\n"`;
4. charger `ConOut->OutputString` depuis `[rcx + 0x08]` et l'appeler;
5. ignorer le statut d'affichage, mettre `rax` à zéro, restaurer la pile et
   retourner au chargeur par `ret`.

La chaîne est exactement la suite UTF-16LE suivante, terminaison comprise :

```text
4E 00 47 00 5F 00 53 00 45 00 45 00 44 00 30 00
5F 00 4F 00 4B 00 0D 00 0A 00 00 00
```

Le marqueur est imprimé une seule fois. Le seed ne boucle pas, ne lit aucune
entrée, n'alloue pas de mémoire, n'accède pas au réseau, au disque, au MMIO ou à
un port I/O durant B0. Il ne valide pas les pointeurs transmis : B0 repose sur la
précondition UEFI que le firmware appelle une application valide avec une
`SystemTable` et un `ConOut` valides. Le producteur B1, nettement plus grand,
doit en revanche valider chaque pointeur avant déréférencement.

### 9.2 B1 producteur

Le seed assembleur B1 affiche un diagnostic ou `NG_NTASM_S0_OK\r\n`, choisit le
statut défini par `ntasm-s0.md`, puis appelle obligatoirement
`BootServices.Exit(ImageHandle, statut, 0, NULL)`. Il ne se contente pas d'un
`ret`, car le chemin producteur possède des handles et allocations à fermer ou
libérer avant la sortie. `Exit` n'est appelé qu'après fermeture explicite des
fichiers encore ouverts et libération des pools qui ne doivent pas survivre.

## 10. Golden B1 de l'image minimale NTASM-S0

Le golden de payload et de relocation défini dans `ntasm-s0.md` produit :

| Champ | Valeur |
|---|---:|
| `.text` RVA / raw / virtual / raw size | `0x1000 / 0x0400 / 0x0008 / 0x0200` |
| `.rdata` RVA / raw / virtual / raw size | `0x2000 / 0x0600 / 0x000E / 0x0200` |
| `.data` RVA / raw / virtual / raw size | `0x3000 / 0x0800 / 0x0008 / 0x0200` |
| `.reloc` RVA / raw / virtual / raw size | `0x4000 / 0x0A00 / 0x000C / 0x0200` |
| `AddressOfEntryPoint` | `0x1000` |
| `SizeOfCode` | `0x0200` |
| `SizeOfInitializedData` | `0x0600` |
| `SizeOfHeaders` | `0x0400` |
| `SizeOfImage` | `0x5000` |
| Base Relocation Directory | RVA `0x4000`, size `0x000C` |
| taille fichier | `0x0C00` |

Les quatre valeurs de `Characteristics` doivent apparaître en little-endian :

```text
.text   20 00 00 60
.rdata  40 00 00 40
.data   40 00 00 C0
.reloc  40 00 00 42
```

Ce golden est structurel; le ledger B0 donne séparément chaque octet de la forme
Hello de `seed0.efi` et son rôle.

## 11. Critères d'acceptation sous QEMU/OVMF

### 11.1 Acceptation du profil B0 validé sur OVMF

Une Gate B0 n'est verte que si toutes les preuves suivantes sont réunies :

1. `seed0.efi` fait exactement `0x600` octets, possède le hash du ledger et est
   placé comme `EFI\BOOT\BOOTX64.EFI` sur une image FAT jetable;
2. OVMF est lancé sans Secure Boot, réseau ou passthrough pour ce seed non signé;
3. le loader accepte le PE sans relocation, entre une fois au RVA `0x1000` et
   n'émet aucune exception CPU ou erreur `LoadImage`;
4. la console UEFI montre exactement une occurrence de `NG_SEED0_OK` suivie de
   CRLF;
5. le `ret` rend proprement le contrôle au firmware, QEMU reste actif et aucun
   reset ou triple fault ne survient;
6. deux boots à froid du même fichier et avec deux magasins de variables frais
   donnent le même marqueur et le même statut;
7. une inspection en lecture seule confirme PE à `0x40`, deux sections `.text`
   RX et `.rdata` R, aucune section W+X, seize data directories toutes nulles,
   aucun import, aucune relocation et aucun overlay;
8. le ledger couvre exactement `0x0000..0x05FF` sans trou ni chevauchement et le
   désassemblage correspond aux huit instructions documentées.

La réussite de ces huit points est la condition « si et seulement si » qui
autorise le profil B0 sans `.reloc`. Elle est satisfaite sur QEMU 10.2.0 avec le
firmware EDK2 local : deux runs indépendants ont produit le marqueur puis rendu le
contrôle à `UiApp`. Cette validation reste limitée au banc consigné dans
`out/b0/20260902-135815-ddaea37914764ec9828b8f6fa4b87a9d/manifest.md` et ne remet pas en cause le profil B1 canonique.

### 11.2 Acceptation du profil B1 producteur

Pour la Gate B1, s'ajoutent : source à la limite acceptée, source trop grande,
malformed corpus, sortie préexistante, écriture courte injectée, PE golden exact
et succès `NG_NTASM_S0_OK` uniquement après `Flush` et `Close` réussis. Une
inspection confirme PE à `0x80`, quatre sections, seul le répertoire Base
Relocation non nul et aucune section W+X. Au moins un run doit charger une sortie
B1 à une adresse différente de `0x0000000140000000`, ce qui exerce réellement
une relocation `DIR64`, puis `Exit` doit rendre proprement la main au firmware.

## 12. Invariants de sécurité et non-objectifs

- Aucun octet source ne décide d'un chemin de fichier, d'une taille de tampon ou
  d'une adresse firmware sans validation de borne.
- Dans B1, aucun pointeur de protocole n'est déréférencé avant validation non
  nulle; B0 conserve son flux exact minimal et sa précondition firmware.
- Aucun calcul de RVA, raw offset, taille alignée ou relocation ne peut
  envelopper `u32`/`u64`.
- Le writer B1 n'émet ni W+X, ni import, ni overlay, ni code auto-modifiant.
- L'image n'est pas déclarée compatible Secure Boot : la signature et
  l'enrôlement de clés sont hors de S0.
- Le checksum PE, Authenticode, ASLR statistique, unwind, debug, ressources,
  pilotes UEFI, runtime services, GOP, série, réseau et accès matériel sont hors
  de ce profil.
- Le support d'un firmware autre qu'OVMF est différé jusqu'à ce que B0 et B1
  soient reproductibles.

Une extension de ce layout exige un nouveau golden d'en-tête, une mise à jour du
ledger, une justification d'octets et la preuve qu'aucun producteur étranger
n'entre dans la chaîne.
