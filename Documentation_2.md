# Documentation KFS-2 (v2) : la Global Descriptor Table (GDT)

> Réécriture complète de `Documentation.md`. Elle corrige les imprécisions de la première version et ajoute ce qui manquait : **le pourquoi** de chaque champ et de chaque valeur.
>
> Fichiers concernés : [src/gdt.h](src/gdt.h), [src/gdt.c](src/gdt.c), [src/gdt_flush.s](src/gdt_flush.s), [src/helpers.c](src/helpers.c), [src/boot.s](src/boot.s).

---

## Sommaire

1. Vue d'ensemble : la segmentation x86
2. Pourquoi KFS-2 installe sa propre GDT
3. Les 7 segments de KFS-2
4. Anatomie d'un descripteur (8 octets)
   - 4.1 L'histoire qui explique tout : du 80286 au 80386
   - 4.2 Vue binaire
   - 4.3 La base
   - 4.4 La limite
   - 4.5 L'octet d'accès
   - 4.6 Les flags (octet `granularity`)
   - 4.7 Exemple : le descripteur Kernel Code octet par octet
5. La structure `gdt_ptr` et le registre GDTR
6. Le code C ligne par ligne (`gdt.h` / `gdt.c`)
7. Les sélecteurs de segment
8. Activer la GDT : `gdt_flush.s`
9. Ce que la GDT protège réellement (et ce qu'elle ne protège pas)
10. Remontée de pile : `print_stack`
11. Annexe : récapitulatif des valeurs

---

## 1. Vue d'ensemble : la segmentation x86

En **mode protégé 32 bits**, le processeur ne manipule jamais une adresse « nue ». Toute adresse utilisée par une instruction est un couple **(segment, offset)** :

```text
adresse linéaire = base du segment + offset
```

- Le **segment** est désigné par un **registre de segment** (`CS`, `DS`, `SS`, `ES`, `FS`, `GS`). Ce registre contient un **sélecteur**, c'est-à-dire un numéro d'entrée dans une table.
- Cette table, c'est la **GDT (Global Descriptor Table)**. Chaque entrée est un **descripteur de segment** de 8 octets qui indique au CPU :
  - **où** commence le segment (*base*),
  - **jusqu'où** on peut aller (*limite*),
  - **ce qu'on a le droit d'y faire** (exécuter, lire, écrire),
  - **qui** a le droit d'y accéder (niveau de privilège, *ring*).

À chaque accès mémoire, le CPU vérifie que l'offset ne dépasse pas la limite et que l'opération est autorisée. Sinon, il lève une exception (#GP, *General Protection Fault*).

> **Métaphore.** Le CPU est un vigile, la RAM est un immeuble et chaque segment est un badge. La GDT est le registre officiel des badges : pour chacun, elle indique la porte d'entrée (base), le nombre d'étages accessibles (limite), les actions permises (accès) et le niveau d'habilitation (DPL).

Comme la pagination n'est pas encore activée dans KFS-2, **adresse linéaire = adresse physique**.

---

## 2. Pourquoi KFS-2 installe sa propre GDT

### 2.1 La GDT de GRUB n'est pas fiable

La spécification Multiboot (que GRUB respecte) dit textuellement que, au moment où le noyau reçoit la main :

> *« Even though the segment registers are set up as described above, the `GDTR` may be invalid, so the OS image must not load any segment registers (even just reloading the same values!) until it sets up its own `GDT`. »*

En clair : GRUB laisse le CPU dans un état utilisable (`CS`, `DS`, `SS`… couvrent déjà 0 à 4 Gio), mais **la table vers laquelle pointe GDTR peut se trouver n'importe où, voire avoir déjà été écrasée**. Le premier rechargement d'un registre de segment, par exemple au retour d'une interruption, pourrait lire des déchets. Le résultat serait une #GP, puis une double faute, puis une **triple faute**, et le CPU redémarre.

Le noyau doit donc construire et charger sa propre GDT **avant** toute opération qui recharge un registre de segment (interruptions, changement de ring, etc.).

### 2.2 Les exigences du sujet

- La GDT doit être placée à l'adresse **`0x00000800`**.
- Elle doit contenir au minimum : *kernel code, kernel data, kernel stack, user code, user data, user stack*.
- (La contrainte « moins de 10 Mo » du sujet porte sur **la taille du travail rendu**, pas sur l'emplacement de la GDT.)

### 2.3 Pourquoi `0x800` est un emplacement sûr

Carte de la mémoire basse au démarrage d'un PC :

| Plage | Contenu |
| :--- | :--- |
| `0x00000` – `0x003FF` | IVT (table des vecteurs d'interruption du mode réel) |
| `0x00400` – `0x004FF` | BDA (BIOS Data Area) |
| **`0x00500` – `0x07BFF`** | **Mémoire conventionnelle libre** ← la GDT est ici |
| `0x07C00` – `0x07DFF` | Ancien secteur de boot (plus utilisé une fois GRUB lancé) |
| `0xB8000` | Mémoire vidéo VGA texte |
| `0x100000` (1 Mio) | Notre noyau (voir `linker.ld`) |

La GDT de KFS-2 occupe 7 × 8 = 56 octets, de `0x800` à `0x837`. Elle est loin du noyau, de la pile et de la VGA.

---

## 3. Les 7 segments de KFS-2

KFS-2 utilise le **Flat Memory Model** (modèle plat) : **tous** les segments ont `base = 0` et couvrent **tout** l'espace de 4 Gio. Ils ne diffèrent que par leurs **droits** et leur **niveau de privilège**.

| Index | Sélecteur | Segment | DPL | `access` | `granularity` | Rôle |
| :---: | :---: | :--- | :---: | :---: | :---: | :--- |
| 0 | `0x00` | Null | — | `0x00` | `0x00` | Obligatoire, jamais utilisable |
| 1 | `0x08` | Kernel Code | 0 | `0x9A` | `0xCF` | Code du noyau (chargé dans `CS`) |
| 2 | `0x10` | Kernel Data | 0 | `0x92` | `0xCF` | Données du noyau (`DS`, `ES`, `FS`, `GS`, actuellement aussi `SS`) |
| 3 | `0x18` | Kernel Stack | 0 | `0x92` | `0xCF` | Pile du noyau (exigée par le sujet) |
| 4 | `0x20` | User Code | 3 | `0xFA` | `0xCF` | Code des futurs programmes utilisateur |
| 5 | `0x28` | User Data | 3 | `0xF2` | `0xCF` | Données utilisateur |
| 6 | `0x30` | User Stack | 3 | `0xF2` | `0xCF` | Pile utilisateur |

### 3.1 Ring 0 et Ring 3

Le x86 définit 4 niveaux de privilège (rings 0 à 3). Presque tous les OS n'en utilisent que deux :

- **Ring 0 (superviseur)** : le noyau. Il peut exécuter les instructions privilégiées (`lgdt`, `cli`, `hlt`, `mov cr0`…) et accéder à tout.
- **Ring 3 (utilisateur)** : les applications. Les instructions privilégiées y lèvent une #GP.

Le niveau courant du CPU s'appelle le **CPL** (*Current Privilege Level*). Il est stocké dans les 2 bits bas de `CS`. C'est pour cela qu'on ne peut pas modifier `CS` avec un simple `mov` (voir §8).

### 3.2 Le descripteur nul (index 0)

L'entrée 0 n'est **jamais lue** par le CPU. Le sélecteur `0x0000` a une signification spéciale, « aucun segment » :

- on a le droit de charger `0` dans `DS`, `ES`, `FS` ou `GS` (pratique pour « désactiver » un registre), mais **tout accès mémoire** à travers lui lève une #GP ;
- charger `0` dans `CS` ou `SS` lève immédiatement une #GP.

Un registre de segment mis à zéro par erreur ne donne donc jamais accès à la mémoire. On écrit quand même 8 octets nuls à cet endroit (`gdt_set_gate(0, 0, 0, 0, 0)`), car la mémoire à `0x800` n'est **pas** initialisée à zéro au démarrage.

### 3.3 Pourquoi 3 segments par ring (code / données / pile) ?

**Code ≠ données** : c'est une obligation matérielle.

- `CS` ne peut contenir qu'un segment **exécutable** (bit `E = 1`).
- Un segment de code n'est **jamais inscriptible** en mode protégé. On ne peut donc pas utiliser le même segment pour le code et pour les variables.
- `SS` exige un segment de **données inscriptible** (`E = 0`, `RW = 1`).

**Données vs pile** : en modèle plat, ils sont **identiques** (`0x92` / `0xF2`). Le matériel n'impose pas de segment de pile séparé. On le crée parce que **le sujet l'exige** et parce que cela permettrait plus tard de lui donner des propriétés différentes (une limite plus petite, ou le mode *expand-down* avec `DC = 1`) sans toucher au segment de données.

> ⚠️ **Remarque sur le code actuel.** [src/gdt_flush.s](src/gdt_flush.s) charge `0x10` (Kernel Data) dans `SS`. Le segment `0x18` (Kernel Stack) est donc **décrit dans la GDT mais jamais utilisé**. Cela fonctionne, puisque les deux descripteurs sont identiques. Pour utiliser réellement le segment de pile, il faudrait charger `0x18` dans `SS`.

**Pourquoi DPL 3 pour User Data et User Stack ?** À cause des règles de chargement :

- pour charger un segment de données dans `DS`/`ES`/`FS`/`GS`, il faut `max(CPL, RPL) ≤ DPL` ;
- pour charger un segment dans `SS`, il faut `CPL = RPL = DPL`, **exactement**.

Un programme en ring 3 ne pourrait donc ni utiliser la pile `0x18` (DPL 0), ni lire les données `0x10` (DPL 0). Il lui faut ses propres segments DPL 3.

---

## 4. Anatomie d'un descripteur (8 octets)

### 4.1 L'histoire qui explique tout : du 80286 au 80386

La question « pourquoi la base et la limite sont-elles coupées en morceaux, dans un ordre aussi bizarre ? » a une réponse **historique**, pas logique.

**1982 : l'Intel 80286** introduit le mode protégé 16 bits. Son descripteur fait déjà 8 octets :

```text
Octet :   0    1    2    3    4      5       6    7
        ┌─────────┬──────────────┬────────┬─────────┐
 80286  │ Limite  │ Base (24 bits│ Access │ RÉSERVÉ │
        │ 16 bits │  0..23)      │        │ (= 0)   │
        └─────────┴──────────────┴────────┴─────────┘
```

Le 286 n'adresse que 16 Mio (2²⁴), donc une base de 24 bits suffit. Une limite de 16 bits limite un segment à 64 Kio. Les 2 derniers octets sont **réservés** et doivent valoir 0.

**1985 : l'Intel 80386** passe au 32 bits. Il faut une base de 32 bits et une limite plus grande. Mais les systèmes écrits pour le 286 doivent continuer à fonctionner **sans modification**. Intel ne peut donc **rien déplacer** dans les octets 0 à 5. La seule place libre, ce sont les 2 octets réservés :

```text
Octet :   0    1    2    3    4      5       6           7
        ┌─────────┬──────────────┬────────┬───────────┬──────────┐
 80386  │ Limite  │ Base 0..23   │ Access │ Flags +   │ Base     │
        │ 0..15   │ (inchangé)   │        │ Lim 16..19│ 24..31   │
        └─────────┴──────────────┴────────┴───────────┴──────────┘
                 ^ identique au 286 ^       ^ nouveaux (ex-réservés) ^
```

- l'octet 6 reçoit les **4 bits manquants de la limite** (16..19) et **4 nouveaux flags** (G, D/B, L, AVL) ;
- l'octet 7 reçoit les **8 bits manquants de la base** (24..31).

Comme un ancien descripteur 286 a ses octets 6 et 7 à zéro, le 386 l'interprète comme `base_high = 0`, `G = 0`, `D/B = 0` (16 bits) : **exactement** ce que voulait le 286. La rétrocompatibilité est parfaite.

**Conséquence directe pour notre `struct gdt_entry`** : chaque champ C correspond à un morceau de ce puzzle historique.

| Champ C | Octets | Origine |
| :--- | :---: | :--- |
| `limit_low` (16 bits) | 0–1 | Limite du 286 |
| `base_low` (16 bits) | 2–3 | Base du 286 (partie basse) |
| `base_middle` (8 bits) | 4 | Base du 286 (partie haute, bits 16..23) |
| `access` (8 bits) | 5 | Octet d'accès du 286 |
| `granularity` (8 bits) | 6 | Ajout du 386 : limite 16..19 + flags |
| `base_high` (8 bits) | 7 | Ajout du 386 : base 24..31 |

**Pourquoi 6 champs, et pas 4 (base, limite, accès, flags) ?** Parce que le C ne permet pas de dire « ce `uint32_t` est rangé en morceaux non contigus ». La base 32 bits est physiquement coupée en trois (octets 2-3, 4 et 7), avec `access` et `granularity` **au milieu**. Il faut donc trois champs. De même, `base_low` + `base_middle` forment les 24 bits d'origine, mais le C n'a pas de type 24 bits : on les sépare en 16 + 8.

**L'ordre des champs est imposé** : en C, les champs d'une structure sont rangés en mémoire **dans l'ordre de déclaration**. Le CPU lit les octets 0 à 7 dans l'ordre ci-dessus, donc la structure doit les déclarer dans cet ordre exact. Comme le x86 est **little-endian**, `limit_low = 0xFFFF` est stocké `FF FF` aux octets 0-1, ce qui est exactement ce qu'attend le CPU.

### 4.2 Vue binaire

Le descripteur vu comme deux mots de 32 bits (convention des manuels Intel) :

```text
Mot haut (octets 4 à 7)
 31        24 23  22  21  20 19     16 15 14 13 12 11      8 7          0
┌────────────┬───┬───┬───┬───┬────────┬──┬─────┬──┬─────────┬────────────┐
│ Base 31..24│ G │D/B│ L │AVL│Lim19..16│P │ DPL │S │  Type   │ Base 23..16│
└────────────┴───┴───┴───┴───┴────────┴──┴─────┴──┴─────────┴────────────┘
   octet 7     └──── octet 6 ─────────┘ └──── octet 5 (access) ┘  octet 4

Mot bas (octets 0 à 3)
 31                              16 15                               0
┌──────────────────────────────────┬──────────────────────────────────┐
│            Base 15..0            │           Limite 15..0           │
└──────────────────────────────────┴──────────────────────────────────┘
          octets 2-3                          octets 0-1
```

### 4.3 La base (32 bits)

Adresse linéaire où commence le segment. Elle est répartie en `base_low` (bits 0..15), `base_middle` (16..23) et `base_high` (24..31).

Dans KFS-2, `base = 0` partout : l'offset **est** l'adresse. Un pointeur C `0xB8000` désigne bien l'octet physique `0xB8000`.

### 4.4 La limite (20 bits) et son interprétation

La limite est la **valeur maximale autorisée pour un offset** (le dernier octet accessible), et non une taille. Elle est répartie en `limit_low` (bits 0..15) et les 4 bits bas de `granularity` (bits 16..19).

Avec 20 bits, la valeur va de `0x00000` à `0xFFFFF`, soit 2²⁰ = 1 048 576 valeurs. Son interprétation dépend du flag **G** :

| G | Unité | Limite effective | Taille max du segment |
| :---: | :--- | :--- | :--- |
| 0 | octet | `limite` | 2²⁰ octets = **1 Mio** |
| 1 | page de 4 Kio | `(limite << 12) \| 0xFFF` | 2²⁰ × 4096 = **4 Gio** |

Calcul exact pour KFS-2 (`limite = 0xFFFFF`, `G = 1`) :

```text
limite effective = (0xFFFFF << 12) | 0xFFF = 0xFFFFFFFF
→ offsets valides : 0x00000000 … 0xFFFFFFFF  →  2³² octets = 4 Gio exactement
```

(L'ancienne doc écrivait « 0xFFFFF × 4096 ≈ 4 Go ». Le bon calcul est **(0xFFFFF + 1) × 4096 = 4 Gio pile**, car la limite désigne le dernier octet, et le CPU complète les 12 bits bas avec des 1.)

**Pourquoi 20 bits seulement ?** C'est encore l'histoire : 16 bits viennent du 286 et il ne restait que 4 bits libres dans l'octet 6. Le flag G a été inventé pour atteindre 4 Gio malgré tout.

### 4.5 L'octet d'accès (`access`, octet 5)

```text
 Bit :   7     6   5     4     3     2     1     0
       ┌─────┬─────────┬─────┬─────┬─────┬─────┬─────┐
       │  P  │   DPL   │  S  │  E  │ DC  │ RW  │  A  │
       └─────┴─────────┴─────┴─────┴─────┴─────┴─────┘
                              └──────── Type ────────┘
```

| Bit | Nom | Rôle | Pourquoi il existe |
| :---: | :--- | :--- | :--- |
| 7 | **P** – Present | `1` = descripteur valide. Avec `0`, charger ce segment lève une exception #NP. | Permettait aux OS des années 80 de « décharger » un segment sur disque et de le recharger à la demande (swap par segment). |
| 6-5 | **DPL** – Descriptor Privilege Level | Ring requis : `00` = 0 (noyau), `11` = 3 (utilisateur). | C'est la base de la protection noyau / utilisateur. |
| 4 | **S** – Descriptor type | `1` = segment de code/données. `0` = descripteur **système** (TSS, LDT, call gate…). | La GDT contient aussi des objets qui ne sont pas des segments mémoire. Ce bit dit comment lire le champ Type. |
| 3 | **E** – Executable | `1` = segment de **code**. `0` = segment de **données**. | Le CPU interdit d'exécuter des données et d'écrire dans du code. |
| 2 | **DC** – Direction / Conforming | **Données** : `0` = le segment s'étend vers le haut (normal), `1` = *expand-down* (pour des piles à limite basse). **Code** : `0` = non conforme (exécutable seulement depuis le ring DPL), `1` = conforme (appelable depuis un ring moins privilégié sans changer de CPL). | Même bit, sens différent selon E : Intel économise les bits. |
| 1 | **RW** – Readable / Writable | **Code** : `1` = on peut aussi *lire* le code (utile pour les constantes dans `.text`). Le code n'est **jamais** inscriptible. **Données** : `1` = inscriptible. Les données sont **toujours** lisibles. | Là aussi, le sens dépend de E. |
| 0 | **A** – Accessed | Mis à `1` **par le CPU** au premier chargement du segment. | Statistique pour l'OS (savoir quels segments servent). |

> **À propos du bit A.** Nos valeurs le laissent à `0`. Au premier chargement d'un sélecteur, le CPU **écrit** donc dans la GDT pour passer A à 1 : après `gdt_flush`, la mémoire à `0x800` contient `0x9B` et `0x93` au lieu de `0x9A` et `0x92`. C'est sans conséquence ici, car la GDT est en RAM inscriptible. Si un jour la GDT se trouve dans une page en lecture seule, il faudra mettre A à 1 d'avance, sinon le CPU déclenchera une faute en essayant d'écrire.

#### Décodage des 4 valeurs utilisées

| Valeur | Binaire | P | DPL | S | E | DC | RW | A | Lecture |
| :---: | :---: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :--- |
| `0x9A` | `1001 1010` | 1 | 00 | 1 | 1 | 0 | 1 | 0 | Code ring 0, exécutable + lisible, non conforme |
| `0x92` | `1001 0010` | 1 | 00 | 1 | 0 | 0 | 1 | 0 | Données ring 0, lecture/écriture, vers le haut |
| `0xFA` | `1111 1010` | 1 | 11 | 1 | 1 | 0 | 1 | 0 | Code ring 3, exécutable + lisible, non conforme |
| `0xF2` | `1111 0010` | 1 | 11 | 1 | 0 | 0 | 1 | 0 | Données ring 3, lecture/écriture, vers le haut |

Astuce de lecture : le **premier chiffre hexa** donne P + DPL + S (`9` = `1001` → ring 0, `F` = `1111` → ring 3). Le **second** donne le type (`A` = `1010` → code lisible, `2` = `0010` → données inscriptibles).

### 4.6 Les flags (octet `granularity`, octet 6)

```text
 Bit :   7     6     5     4     3   2   1   0
       ┌─────┬─────┬─────┬─────┬───────────────┐
       │  G  │ D/B │  L  │ AVL │ Limite 16..19 │
       └─────┴─────┴─────┴─────┴───────────────┘
         └──── flags (4 bits hauts) ────┘
```

| Bit | Nom | Rôle |
| :---: | :--- | :--- |
| 7 | **G** – Granularity | `0` : limite en octets. `1` : limite en pages de 4 Kio (voir §4.4). |
| 6 | **D/B** – Default / Big | **Code** (D) : `1` = opérandes et adresses 32 bits par défaut, `0` = 16 bits. **Pile** (B) : `1` = `push`/`pop` utilisent `ESP` (32 bits), `0` = `SP` (16 bits). **Données expand-down** : borne haute à 4 Gio au lieu de 64 Kio. |
| 5 | **L** – Long mode | `1` = segment de code 64 bits. Doit valoir `0` en 32 bits (si L = 1, D doit valoir 0). |
| 4 | **AVL** – Available | Libre pour l'OS. Le CPU l'ignore. |
| 3-0 | **Limite 16..19** | Les 4 bits hauts de la limite. |

**`0xCF` = `1100 1111`** :

- `1` G : la limite compte en pages de 4 Kio, donc 4 Gio ;
- `1` D/B : segment 32 bits. C'est **crucial** : avec `D = 0` dans `CS`, le CPU décoderait notre code compilé en 32 bits comme du code 16 bits, et ce serait le crash immédiat ;
- `0` L : pas de 64 bits ;
- `0` AVL : inutilisé ;
- `1111` : les bits 16..19 de la limite. **Attention**, dans notre code ce demi-octet est **ignoré** (voir §6.3).

### 4.7 Exemple : le descripteur Kernel Code octet par octet

`gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF)` produit, à l'adresse `0x808` :

| Adresse | Octet | Champ | Valeur |
| :---: | :---: | :--- | :---: |
| `0x808` | 0 | `limit_low` (bits bas) | `FF` |
| `0x809` | 1 | `limit_low` (bits hauts) | `FF` |
| `0x80A` | 2 | `base_low` | `00` |
| `0x80B` | 3 | `base_low` | `00` |
| `0x80C` | 4 | `base_middle` | `00` |
| `0x80D` | 5 | `access` | `9A` |
| `0x80E` | 6 | `granularity` | `CF` |
| `0x80F` | 7 | `base_high` | `00` |

Lu comme un entier 64 bits : **`0x00CF9A000000FFFF`**. On retrouve cette valeur « magique » dans presque tous les tutoriels OSDev.

Vérification possible dans QEMU (moniteur, `Ctrl+Alt+2`) : `xp /7gx 0x800`.

---

## 5. La structure `gdt_ptr` et le registre GDTR

Le CPU possède un registre spécial, **GDTR**, qui contient l'adresse et la taille de la GDT active. On ne peut pas l'écrire avec `mov` : il faut l'instruction `lgdt`, qui lit en mémoire une structure de **6 octets** (2 + 4) :

```c
struct gdt_ptr {
    uint16_t limit;  // taille de la table en octets, MOINS 1
    uint32_t base;   // adresse linéaire de la table (0x00000800)
} __attribute__((packed));
```

**Pourquoi « taille − 1 » ?** Comme pour un segment, `limit` désigne le **dernier octet valide**, pas une taille. Grâce à cela, 16 bits suffisent pour décrire une table de 65 536 octets (`limit = 0xFFFF`), soit 8192 descripteurs. Ce maximum correspond exactement aux 13 bits d'index d'un sélecteur (§7). Pour nous : `7 × 8 − 1 = 55 = 0x37`.

Le CPU utilise aussi cette limite comme garde-fou : charger un sélecteur dont l'index dépasse la table (par exemple `0x38`, index 7) lève une #GP.

### `packed` : indispensable ici, superflu pour `gdt_entry`

Le compilateur aligne naturellement un `uint32_t` sur une adresse multiple de 4. **Sans** `packed`, `gdt_ptr` deviendrait :

```text
octets 0-1 : limit
octets 2-3 : PADDING (bourrage)   ← inséré par le compilateur
octets 4-7 : base
```

`lgdt` lirait alors la base aux octets 2-5, c'est-à-dire 2 octets de bourrage et la moitié basse de la vraie base. Le CPU chargerait une adresse fausse, ce qui mène tout droit à la triple faute. **`packed` est donc vital pour `gdt_ptr`.**

Pour `gdt_entry`, l'ordre des champs (16, 16, 8, 8, 8, 8) tombe naturellement sans trou : `sizeof` vaut 8 même sans `packed`. On le met quand même, pour **documenter l'intention** (« cette structure est un format matériel ») et pour ne pas dépendre des règles d'alignement d'un compilateur ou d'une architecture.

### Où vit `gp` ?

`static struct gdt_ptr gp;` est rangé dans `.bss` (au-dessus de 1 Mio). Ce n'est pas un problème : `lgdt` **copie** les 6 octets dans GDTR. Après l'instruction, le CPU ne relit plus jamais `gp`. Seule la table à `0x800` doit rester intacte.

---

## 6. Le code C ligne par ligne

### 6.1 Placer la table à une adresse fixe

```c
static struct gdt_entry *gdt = (struct gdt_entry *)0x00000800;
static struct gdt_ptr gp;
```

**Pourquoi un pointeur et pas un tableau `struct gdt_entry gdt[7];` ?** Un tableau serait placé par l'éditeur de liens dans `.bss`, quelque part après 1 Mio. Il n'y a **aucun moyen** en C standard de dire « mets ce tableau à `0x800` ». On prend donc un pointeur et on lui donne l'adresse imposée par le sujet. `gdt[num]` s'utilise ensuite exactement comme un tableau : `gdt[1]` est à `0x800 + 1 × 8 = 0x808`.

Le cast `(struct gdt_entry *)0x00000800` est légal en environnement *freestanding* : la pagination est désactivée et l'adresse physique `0x800` existe en RAM.

**Pourquoi `static` ?** Les deux variables ne sont visibles que dans `gdt.c`. Personne d'autre ne doit modifier la GDT directement.

### 6.2 `gdt_set_gate` : traduire des valeurs « humaines » en format matériel

```c
static void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t gran)
```

La fonction prend les paramètres **logiques** d'un segment (une base 32 bits entière, une limite entière, deux octets de droits) et se charge du découpage. L'appelant n'a jamais à penser à l'éparpillement historique des bits : c'est une **couche d'abstraction**.

```c
    gdt[num].base_low    = (base & 0xFFFF);        // bits 0..15
    gdt[num].base_middle = (base >> 16) & 0xFF;    // bits 16..23
    gdt[num].base_high   = (base >> 24) & 0xFF;    // bits 24..31
```

Rappel des opérateurs :

- `x & 0xFFFF` **masque** : on garde les 16 bits bas et on met le reste à 0 ;
- `x >> 16` **décale** vers la droite : les bits 16..31 descendent en position 0..15 ;
- `(x >> 16) & 0xFF` combine les deux : on extrait l'octet qui était aux bits 16..23.

Exemple avec une base fictive `0x12345678` : `base_low = 0x5678`, `base_middle = 0x34`, `base_high = 0x12`.

```c
    gdt[num].limit_low   = (limit & 0xFFFF);       // bits 0..15 de la limite
    gdt[num].granularity = (limit >> 16) & 0x0F;   // bits 16..19 dans le demi-octet bas
```

```c
    gdt[num].granularity |= (gran & 0xF0);          // flags dans le demi-octet haut
    gdt[num].access      = access;
```

- `gran & 0xF0` ne garde **que** les 4 bits de flags ;
- `|=` les **fusionne** avec les bits de limite déjà écrits, sans les écraser.

Avec `gran = 0xCF` et `limit = 0xFFFFFFFF` : `0x0F | 0xC0 = 0xCF`.

### 6.3 Pourquoi les appels sont écrits comme ça

```c
gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
```

| Argument | Valeur | Raison |
| :--- | :--- | :--- |
| `num` | `1` | Index dans la table. Le sélecteur vaut `num × 8` (§7). L'ordre 0 à 6 suit la liste du sujet. |
| `base` | `0` | Modèle plat : tous les segments démarrent à 0. GRUB utilise déjà des segments plats à base 0, donc **toutes les adresses déjà en usage** (`ESP`, `EIP`, `0xB8000`…) restent valides après le changement de GDT. Une autre base décalerait tout et ferait planter le noyau instantanément. |
| `limit` | `0xFFFFFFFF` | « Tout l'espace ». Seuls les 20 bits bas (`0xFFFFF`) sont réellement stockés, le reste est perdu par les masques. On aurait pu écrire `0xFFFFF`, le résultat serait identique. `0xFFFFFFFF` se lit simplement plus facilement comme « jusqu'à la fin de la mémoire 32 bits ». |
| `access` | `0x9A`, `0x92`, `0xFA`, `0xF2` | Type et ring du segment (§4.5). |
| `gran` | `0xCF` | Flags `0xC` (4 Kio + 32 bits). Le `F` du bas est une **convention héritée** des tutoriels, où `0xCF` est la valeur exacte de l'octet 6. Dans notre fonction, il est **ignoré** par `& 0xF0`, car les bits de limite viennent du paramètre `limit`. `0xC0` donnerait exactement le même descripteur. |

Pourquoi **les mêmes valeurs** pour tous les segments sauf `access` ? En modèle plat, la segmentation ne sert plus à découper la mémoire, seulement à **définir des types et des privilèges**. Le découpage et l'isolation de la mémoire seront confiés plus tard à la **pagination** (§9). C'est le choix de Linux, Windows, et de tous les OS modernes en 32 bits.

### 6.4 `init_gdt`

```c
void init_gdt(void) {
    gp.limit = (sizeof(struct gdt_entry) * 7) - 1;  // 55 = 0x37
    gp.base  = (uint32_t)gdt;                       // 0x00000800

    gdt_set_gate(0, 0, 0, 0, 0);                    // Null
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);     // Kernel Code
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);     // Kernel Data
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0x92, 0xCF);     // Kernel Stack
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xCF);     // User Code
    gdt_set_gate(5, 0, 0xFFFFFFFF, 0xF2, 0xCF);     // User Data
    gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF2, 0xCF);     // User Stack

    gdt_flush((uint32_t)&gp);
}
```

- `sizeof(struct gdt_entry)` plutôt que `8` : si la structure était mal définie (padding), le calcul resterait cohérent avec ce qui est réellement en mémoire.
- `(uint32_t)gdt` : en 32 bits, un pointeur tient dans un `uint32_t`. On convertit l'adresse en nombre pour le champ `base`.
- L'ordre compte : **on remplit la table avant de la charger**. Charger GDTR vers une table vide ou incomplète, puis recharger les segments, provoquerait une triple faute.

---

## 7. Les sélecteurs de segment

La valeur placée dans un registre de segment n'est **pas** un simple index. C'est un **sélecteur** de 16 bits :

```text
 15                                  3   2    1   0
┌─────────────────────────────────────┬────┬────────┐
│          Index (13 bits)            │ TI │  RPL   │
└─────────────────────────────────────┴────┴────────┘
```

| Champ | Rôle |
| :--- | :--- |
| **Index** | Numéro de l'entrée dans la table. Le CPU calcule `adresse = base_GDT + index × 8`. |
| **TI** (Table Indicator) | `0` = GDT, `1` = LDT (une table locale, non utilisée ici). |
| **RPL** (Requested Privilege Level) | Ring « demandé » par le code qui charge le sélecteur. Pour `CS`, ces 2 bits **sont** le CPL. |

C'est pour cela que les sélecteurs vont de 8 en 8 : `index << 3` revient à multiplier par 8, et TI = RPL = 0 pour le noyau.

| Index | Sélecteur noyau (RPL 0) | Sélecteur à utiliser depuis le ring 3 (RPL 3) |
| :---: | :---: | :---: |
| 1 Kernel Code | `0x08` | — |
| 2 Kernel Data | `0x10` | — |
| 3 Kernel Stack | `0x18` | — |
| 4 User Code | `0x20` | **`0x23`** |
| 5 User Data | `0x28` | **`0x2B`** |
| 6 User Stack | `0x30` | **`0x33`** |

Pour passer en ring 3 plus tard, on chargera `CS = 0x23` et `SS = 0x33` (RPL = 3), et non `0x20`/`0x30`. Ce point est absent de la première version de la doc.

---

## 8. Activer la GDT : `gdt_flush.s`

### 8.1 Le cache caché des registres de segment (la vraie raison du « flush »)

Chaque registre de segment a deux parties :

```text
       partie visible        partie cachée (descriptor cache)
     ┌──────────────┐  ┌────────────────────────────────────────┐
 DS  │  sélecteur   │  │  base │ limite │ droits (copie du desc.) │
     └──────────────┘  └────────────────────────────────────────┘
```

Quand on écrit un sélecteur dans `DS`, le CPU va lire **une seule fois** le descripteur dans la GDT et le copie dans la partie cachée. Ensuite, **il ne relit plus la GDT** : tous les accès utilisent le cache.

Conséquence : `lgdt` change GDTR, mais **ne touche pas** aux caches. Les registres continuent d'utiliser les descripteurs de GRUB copiés plus tôt. Pour que notre GDT soit réellement utilisée, il faut **recharger chaque registre de segment**, ce qui force une relecture dans la nouvelle table. C'est ce « flush ».

### 8.2 Le code (syntaxe AT&T / GAS, celle du projet)

```asm
.global gdt_flush
.type gdt_flush, @function

gdt_flush:
    /* 1. Récupérer l'adresse de gdt_ptr passée en paramètre (sur la pile) */
    movl 4(%esp), %eax
    lgdt (%eax)             /* Charger la GDT dans GDTR */

    /* 2. Mettre à jour les registres de segment de données avec 0x10 (Kernel Data) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 3. Far jump pour recharger CS avec 0x08 (Kernel Code) */
    ljmp $0x08, $.reload_cs

.reload_cs:
    ret
```

> En AT&T, l'ordre est `source, destination` (`movl 4(%esp), %eax` signifie `eax ← [esp+4]`). Les registres sont préfixés par `%`, les constantes par `$`, et le suffixe (`l`, `w`) donne la taille (32 ou 16 bits). L'ancienne doc montrait ce code en syntaxe Intel/NASM, qui **ne compilerait pas** avec `gcc -c` sur un fichier `.s`.

### 8.3 Étape 1 : charger GDTR

- **`4(%esp)`** : convention d'appel *cdecl*. `call gdt_flush` a empilé l'adresse de retour, qui se trouve en `0(%esp)`. L'argument `(uint32_t)&gp`, empilé juste avant, est donc en `4(%esp)`.
- **`lgdt (%eax)`** : lit 6 octets à l'adresse contenue dans `EAX` (`limit` sur 2 octets, puis `base` sur 4) et les place dans GDTR. C'est une instruction **privilégiée** : elle n'est autorisée qu'en ring 0.

### 8.4 Étape 2 : recharger les segments de données

- `0x10` = index 2 (Kernel Data), TI = 0, RPL = 0.
- On passe par `AX` parce que le x86 n'a pas d'instruction `mov immédiat → registre de segment`. Il faut passer par un registre général.
- Chaque `movw %ax, %xs` déclenche la lecture du descripteur 2 dans la GDT, les vérifications (présent ? DPL compatible ? type correct ?), puis le remplissage du cache.
- `SS` reçoit aussi `0x10`. Le passage est transparent : l'ancien `SS` de GRUB avait déjà base 0 et limite 4 Gio, donc `ESP` pointe toujours au même endroit. (Pour utiliser le segment Kernel Stack dédié, on écrirait `movw $0x18, %ax` puis `movw %ax, %ss`.)

### 8.5 Étape 3 : recharger `CS` par un saut lointain

- `mov` vers `CS` est **interdit** (le CPU lève #UD). En effet, changer `CS` change à la fois *où* l'on exécute et *à quel ring*. Le CPU impose de le faire en même temps qu'un changement d'`EIP`, via un saut, un appel ou un retour **lointain** (*far*).
- `ljmp $0x08, $.reload_cs` charge `CS ← 0x08` (et donc son cache depuis le descripteur 1) et `EIP ← .reload_cs`. Comme `.reload_cs` est l'instruction suivante, l'exécution continue « au même endroit », mais avec le nouveau `CS`.
- Ce saut vide aussi la file d'instructions préchargées.

### 8.6 Le `ret` final

L'adresse de retour empilée par `call` est toujours sur la pile, et `SS` (base 0) la trouve au même endroit. `ret` rend la main à `init_gdt()`, et tout le noyau tourne désormais sur **notre** GDT.

### 8.7 Pourquoi aucune interruption ne vient perturber la séquence

Multiboot garantit que GRUB rend la main avec les interruptions **désactivées** (`IF = 0`), et KFS-2 ne les réactive pas. Personne ne peut donc interrompre le code entre `lgdt` et le dernier rechargement, à un moment où l'état est à moitié ancien, à moitié nouveau.

---

## 9. Ce que la GDT protège réellement (et ce qu'elle ne protège pas)

**Ce qu'elle protège :**

- le code ring 3 ne peut pas charger les segments noyau (DPL 0) dans ses registres ;
- le code ring 3 ne peut pas exécuter d'instructions privilégiées (`lgdt`, `cli`, `hlt`…) ;
- on ne peut pas exécuter un segment de données, ni écrire dans un segment de code.

**Ce qu'elle ne protège pas (en modèle plat) :** comme User Data couvre **aussi** 0 à 4 Gio, un programme ring 3 pourrait, *avec ses propres segments*, lire et écrire la mémoire du noyau. L'isolation de la mémoire viendra de la **pagination** (bit U/S des pages), lors d'une étape ultérieure de KFS.

**Ce qu'il faudra ajouter plus tard :** un **TSS** (*Task State Segment*). C'est un descripteur système (`S = 0`) qui indique au CPU quelle pile noyau (`SS0:ESP0`) utiliser quand une interruption arrive en ring 3. Sans lui, le passage ring 3 → ring 0 est impossible.

---

## 10. Remontée de pile : `print_stack`

### 10.1 Objectif

Sans OS en dessous, il n'y a ni débogueur ni « Segmentation fault » détaillé. `print_stack` (dans [src/helpers.c](src/helpers.c)) affiche la **chaîne des appels** en cours, ce qu'on appelle une *backtrace*.

### 10.2 Organisation de la pile x86 (32 bits)

La pile grandit vers les **adresses basses**. `call` empile l'adresse de retour. Ensuite, le prologue généré par GCC (`push %ebp ; mov %esp, %ebp`) sauvegarde l'ancien `EBP` et fait pointer `EBP` sur cette sauvegarde :

```text
            adresses hautes  (stack_top : la pile commence ici)
          ┌──────────────────────────────┐
          │ arguments de la fonction     │
          ├──────────────────────────────┤
EBP + 4 → │ adresse de retour            │  frame[1]
          ├──────────────────────────────┤
EBP     → │ EBP de l'appelant (sauvegardé)│ frame[0]
          ├──────────────────────────────┤
          │ variables locales            │
ESP     → └──────────────────────────────┘  ← sommet actuel de la pile
            adresses basses  (stack_bottom : limite à ne pas dépasser)
```

> Attention au vocabulaire : le **sommet** de la pile est là où pointe `ESP`, et il descend. `stack_top` (adresse haute) est l'endroit où la pile **commence**, et `stack_bottom` (adresse basse) la **limite** qu'elle ne doit pas franchir. L'ancienne doc mélangeait les deux.

Chaque cadre contient l'`EBP` du cadre parent, ce qui forme une **liste chaînée** de cadres qu'on peut parcourir. Cela ne marche que parce que le Makefile compile avec **`-fno-omit-frame-pointer`**. Sans cette option, GCC en `-O2` utilise `EBP` comme registre ordinaire et la chaîne n'existe plus.

### 10.3 Le code expliqué

```c
void print_stack(void)
{
    uint32_t *frame;
    uint32_t frame_address;
    uint32_t stack_start = (uint32_t)stack_bottom;   // borne basse de la pile noyau
    uint32_t stack_end = (uint32_t)stack_top;        // borne haute
    uint32_t frame_number = 0;

    __asm__ volatile ("movl %%ebp, %0" : "=r"(frame));  // EBP courant → frame
    printk("Kernel stack trace:\n");

    while (frame_number < 32) {                      // garde-fou anti-boucle infinie
        frame_address = (uint32_t)frame;
        if (frame_address < stack_start || frame_address + 8 > stack_end) {
            printk("  invalid frame at ");           // EBP hors de la pile noyau
            printk_hex(frame_address);
            printk("\n");
            return;
        }

        printk("  #");
        printk_hex(frame_number);
        printk(" frame=");
        printk_hex(frame_address);
        printk(" return=");
        printk_hex(frame[1]);                        // adresse de retour
        printk("\n");

        if (frame[0] <= frame_address) {             // le parent doit être plus haut
            return;
        }
        frame = (uint32_t *)frame[0];                // remonter au cadre parent
        frame_number++;
    }

    printk("  ... stack trace truncated\n");
}
```

| Élément | Explication |
| :--- | :--- |
| `stack_bottom` / `stack_top` | Symboles définis dans [src/boot.s](src/boot.s) autour des 16 Kio réservés dans `.bss`, déclarés `extern char[]` dans `helpers.c`. |
| `movl %%ebp, %0` | Assembleur inline : copie `EBP` dans `frame`. `%%` échappe le `%` du registre. `"=r"` indique « sortie dans un registre général ». `volatile` interdit à GCC de supprimer ou déplacer l'instruction. |
| `frame_address + 8 > stack_end` | On va lire `frame[0]` et `frame[1]` (8 octets) : ils doivent être entièrement dans la pile. |
| `frame[0] <= frame_address` | Un `EBP` parent est forcément **plus haut** que l'enfant. Sinon la pile est corrompue, ou on a atteint le premier cadre (`_start` n'a pas de prologue, donc l'`EBP` laissé par GRUB n'est pas dans notre pile). |
| Limite de 32 | Garantit la terminaison même si la chaîne forme une boucle. |

### 10.4 Lire la sortie

```text
Kernel stack trace:
  #0x00000000 frame=0x00105fe8 return=0x001002ee
  #0x00000001 frame=0x00106008 return=0x001002fd
```

- **#0** : cadre de `print_stack`. `return` est l'adresse dans `kernel_main` juste après `call print_stack`.
- **#1** : cadre de `kernel_main` (adresse plus haute, comme prévu). `return` pointe dans `_start` (boot.s), juste après `call kernel_main`.
- Ensuite, l'`EBP` sauvegardé est celui de GRUB, hors de notre pile : la remontée s'arrête.

Pour retrouver la fonction à partir d'une adresse : `addr2line -e mykernel.bin 0x001002ee` ou `objdump -d mykernel.bin`.

### 10.5 `printk_hex`

```c
buffer[2 + i] = digits[(value >> (28 - i * 4)) & 0xf];
```

Un chiffre hexadécimal représente 4 bits. Pour `i = 0`, on décale de 28 pour amener les bits 28..31 en bas, puis `& 0xf` isole ce quartet, qui sert d'index dans `"0123456789abcdef"`. On répète 8 fois (8 × 4 = 32 bits), avec le préfixe `0x` et le `\0` final, soit 11 caractères.

---

## 11. Annexe : récapitulatif des valeurs

```text
Adresse   Index  Sélecteur  Descripteur (64 bits)   Segment
0x800     0      0x00       0x0000000000000000      Null
0x808     1      0x08       0x00CF9A000000FFFF      Kernel Code  (ring 0)
0x810     2      0x10       0x00CF92000000FFFF      Kernel Data  (ring 0)
0x818     3      0x18       0x00CF92000000FFFF      Kernel Stack (ring 0)
0x820     4      0x20/0x23  0x00CFFA000000FFFF      User Code    (ring 3)
0x828     5      0x28/0x2B  0x00CFF2000000FFFF      User Data    (ring 3)
0x830     6      0x30/0x33  0x00CFF2000000FFFF      User Stack   (ring 3)

GDTR : limit = 0x0037 (55), base = 0x00000800
```

(Après le premier chargement, le bit A des segments utilisés passe à 1 : `9A → 9B`, `92 → 93`.)

### Pour aller plus loin

- Intel® 64 and IA-32 Architectures SDM, Vol. 3A, chapitre 3 (*Protected-Mode Memory Management*), §3.4.5 *Segment Descriptors*.
- Spécification Multiboot 0.6.96, §3.2 *Machine state*.
- OSDev Wiki : *Global Descriptor Table*, *GDT Tutorial*, *Segmentation*.
