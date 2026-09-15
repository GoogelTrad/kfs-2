# Documentation KFS-2 : la Global Descriptor Table (GDT)

## 1. Vue d'ensemble

La **GDT (Global Descriptor Table)** est une structure de données obligatoire en **mode protégé 32 bits x86**. Elle décrit la manière dont le processeur accède à la mémoire et applique les droits associés à chaque segment.

### La métaphore du contrôle d'accès

On peut imaginer le processeur comme un vigile matériel et la mémoire RAM comme un immeuble. Le processeur n'autorise pas un accès direct à une adresse mémoire : chaque instruction doit utiliser un badge d'accès, appelé *segment*.

La GDT est la liste officielle de ces badges. Pour chaque zone mémoire, elle indique :

- **L'emplacement :** l'adresse de début (*base*) et la taille maximale (*limite*).
- **Les droits d'accès :** lecture, écriture et/ou exécution.
- **Le niveau de privilège :** noyau (*Ring 0*) ou utilisateur (*Ring 3*).

## 2. Pourquoi réimplémenter la GDT dans KFS-2 ?

Au démarrage, le bootloader (**GRUB**) crée une GDT temporaire pour charger le noyau en mémoire. KFS-2 doit cependant installer sa propre GDT pour deux raisons :

1. **Éviter une dépendance à GRUB :** l'emplacement de la GDT de GRUB n'est pas garanti. Si le noyau écrase cette zone, le processeur peut subir un crash irrécupérable (**triple fault**) et redémarrer.
2. **Respecter le sujet :** la GDT principale doit être placée à l'adresse fixe **`0x00000800`**, dans la limite d'espace imposée (**moins de 10 Mo**).

## 3. Architecture des segments

En mode 32 bits, KFS-2 utilise un modèle **Flat Memory Model** (*mode plat*). Les segments de code et de données couvrent la même plage, de `0x00000000` à `0xFFFFFFFF` (**4 Go**), au lieu de découper la mémoire en petites zones.

La GDT contient **7 descripteurs de 8 octets** :

| Index | Sélecteur | Descripteur | Privilège | Rôle |
| :---: | :---: | :--- | :---: | :--- |
| 0 | `0x00` | **Null Descriptor** | — | Entrée obligatoire sur x86. Intercepte les pointeurs invalides. |
| 1 | `0x08` | **Kernel Code** | Ring 0 | Autorise l'exécution des instructions du noyau. |
| 2 | `0x10` | **Kernel Data** | Ring 0 | Autorise la lecture et l'écriture des données du noyau. |
| 3 | `0x18` | **Kernel Stack** | Ring 0 | Segment réservé à la pile du noyau. |
| 4 | `0x20` | **User Code** | Ring 3 | Segment prévu pour les futures applications utilisateur. |
| 5 | `0x28` | **User Data** | Ring 3 | Segment prévu pour les données utilisateur. |
| 6 | `0x30` | **User Stack** | Ring 3 | Segment réservé à la pile utilisateur. |

### Ring 0 et Ring 3

- **Ring 0 (Supervisor) :** privilège maximal réservé au noyau. Il permet d'exécuter les instructions privilégiées et d'accéder au matériel.
- **Ring 3 (User) :** privilège restreint destiné aux applications. Une erreur dans une application ne doit pas pouvoir faire tomber tout le système.

## 4. Structures de données

Un descripteur GDT fait **exactement 8 octets**. Pour des raisons historiques, liées notamment à la compatibilité avec l'Intel 80286, la base (32 bits) et la limite (20 bits) sont réparties dans plusieurs champs.

### Structure `gdt_entry`

```c
struct gdt_entry {
    uint16_t limit_low;    // Bits 0 a 15 de la limite
    uint16_t base_low;     // Bits 0 a 15 de l'adresse de base
    uint8_t  base_middle;  // Bits 16 a 23 de l'adresse de base
    uint8_t  access;       // Octet d'acces (privileges, type de segment)
    uint8_t  granularity;  // Granularite (4 bits) + bits 16 a 19 de la limite
    uint8_t  base_high;    // Bits 24 a 31 de l'adresse de base
} __attribute__((packed));
```

`__attribute__((packed))` empêche le compilateur d'insérer des octets de bourrage (*padding*). La structure occupe donc précisément 8 octets en mémoire.

### Octet d'accès (`access`)

| Valeur | Segment | Décomposition |
| :---: | :--- | :--- |
| `0x9A` | Kernel Code | `10011010b` : présent, Ring 0, exécutable et lisible. |
| `0x92` | Kernel Data / Stack | `10010010b` : présent, Ring 0, segment de données lisible et inscriptible. |
| `0xFA` | User Code | `11111010b` : présent, Ring 3, exécutable et lisible. |
| `0xF2` | User Data / Stack | `11110010b` : présent, Ring 3, segment de données lisible et inscriptible. |

### Structure `gdt_ptr`

Cette structure de **6 octets** est transmise au registre GDTR par l'instruction assembleur `lgdt` :

```c
struct gdt_ptr {
    uint16_t limit; // Taille totale de la table en octets moins 1
    uint32_t base;  // Adresse mémoire physique de la table (0x00000800)
} __attribute__((packed));
```

## 5. Séquence d'initialisation

L'activation de la nouvelle GDT se déroule en trois étapes :

1. **Initialisation en mémoire :** l'adresse `0x00000800` est remplie avec les 7 descripteurs grâce à `gdt_set_gate()`.
2. **Chargement matériel :** l'instruction `lgdt` reçoit un pointeur vers `gdt_ptr` et met à jour le registre GDTR du processeur.
3. **Mise à jour des registres de segment :** les registres de données (`DS`, `ES`, `FS`, `GS`, `SS`) peuvent être chargés avec `mov`. Le registre de code (`CS`) nécessite quant à lui un *far jump* (*saut distant*).

### Fonction `gdt_flush`

```asm
global gdt_flush

gdt_flush:
    mov eax, [esp + 4]    ; Charge l'adresse du gdt_ptr passe en parametre
    lgdt [eax]             ; Informe le CPU de l'emplacement de la nouvelle GDT

    mov ax, 0x10           ; Offset 0x10 = Kernel Data (entree 2 * 8 octets)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax             ; Met a jour les registres de donnees et de pile

    jmp 0x08:.reload_cs    ; Far jump vers le segment Kernel Code (offset 0x08)

.reload_cs:
    ret                    ; Retour au code C avec la nouvelle GDT activee
```

## 6. Anatomie détaillée d'un descripteur GDT

Pour bien comprendre la structure `gdt_entry`, il faut comprendre une contrainte historique majeure d'Intel : la rétrocompatibilité.

Lorsqu'Intel a créé le mode 32 bits avec le processeur 80386, il a fallu conserver la structure 16 bits du processeur précédent, le 80286, tout en étendant la mémoire à 4 Go. Plutôt que de repartir d'une structure propre, Intel a « éparpillé » les nouveaux bits à travers l'entrée.

### 6.1 Vue d'ensemble binaire

Chaque entrée fait exactement **64 bits**, soit **8 octets**. En mémoire, les bits sont organisés ainsi :

```text
63          56 55      52 51      48 47          40 39          32
┌──────────────┬──────────┬──────────┬──────────────┬──────────────┐
│ Base [31..24]│ Flags    │ Limit    │ Access Byte  │ Base [23..16]│
│  (8 bits)    │ (4 bits) │ [19..16] │  (8 bits)    │  (8 bits)    │
└──────────────┴──────────┴──────────┴──────────────┴──────────────┘
 31                                       16 15                       0
┌───────────────────────────────────────────┬──────────────────────────┐
│              Base [15..0]                 │       Limit [15..0]      │
│               (16 bits)                   │         (16 bits)        │
└───────────────────────────────────────────┴──────────────────────────┘
```

Dans le code C, cela correspond à la découpe suivante :

```c
struct gdt_entry {
    uint16_t limit_low;    // Octets 0-1 : bits 0..15 de la limite
    uint16_t base_low;     // Octets 2-3 : bits 0..15 de la base
    uint8_t  base_middle;  // Octet 4    : bits 16..23 de la base
    uint8_t  access;       // Octet 5    : octet d'accès (droits et privilèges)
    uint8_t  granularity;  // Octet 6    : flags + bits 16..19 de la limite
    uint8_t  base_high;    // Octet 7    : bits 24..31 de la base
} __attribute__((packed));
```

### 6.2 Décomposition champ par champ

#### A. La base (32 bits au total)

La base indique l'adresse mémoire physique où commence le segment.

En C, elle est découpée en trois morceaux :

- `base_low` : 16 bits.
- `base_middle` : 8 bits.
- `base_high` : 8 bits.

Dans le **Flat Model** utilisé par KFS-2, la base vaut `0x00000000` pour tous les segments : le segment commence au tout début de la mémoire.

#### B. La limite (20 bits au total)

La limite définit la taille maximale du segment. Elle est découpée en deux morceaux :

- `limit_low` : 16 bits.
- Les 4 bits de poids faible de `granularity` : bits 16 à 19.

Avec 20 bits, il est possible d'exprimer un nombre entre $0$ et $1\text{ Mo}$ :

$$
2^{20} - 1 = \text{0xFFFFF}
$$

Pour couvrir les 4 Go de mémoire avec seulement 20 bits, on utilise le drapeau de granularité décrit plus loin.

#### C. L'octet d'accès (`access`) - octet 5

C'est le champ le plus important pour la sécurité du noyau. Il contrôle qui a le droit d'accéder au segment et ce qu'il peut faire avec.

```text
Bit :  7       6 5       4   3   2   1   0
      ┌───┬───────┬───┬───┬───┬───┬───┐
      │ P │  DPL  │ S │ E │DC │RW │ A │
      └───┴───────┴───┴───┴───┴───┴───┘
```

| Bits | Nom | Signification / valeurs |
| :---: | :--- | :--- |
| 7 | `P` (Present) | `1` si le segment est valide et présent en mémoire RAM. |
| 6-5 | `DPL` (Descriptor Privilege Level) | Niveau de privilège requis : `00` = Ring 0 (noyau), `11` = Ring 3 (utilisateur). |
| 4 | `S` (Descriptor type) | `1` pour un segment de code ou de données ; `0` pour un descripteur système (TSS, LDT...). |
| 3 | `E` (Executable) | `1` = segment de code exécutable ; `0` = segment de données ou de pile. |
| 2 | `DC` (Direction/Conforming) | Pour les données : `0` signifie que le segment grandit vers le haut. Pour le code : `0` signifie que le code ne peut être exécuté que depuis le niveau DPL exact. |
| 1 | `RW` (Read/Write) | Pour le code : `1` autorise la lecture du code. Pour les données : `1` autorise l'écriture. |
| 0 | `A` (Accessed) | Mis à `1` automatiquement par le CPU lorsque le segment est accédé. Laisser à `0` par défaut. |

##### Exemples dans KFS-2

- **`0x9A` (Kernel Code) :** `10011010b` → présent, Ring 0, code exécutable et lisible.
- **`0x92` (Kernel Data/Stack) :** `10010010b` → présent, Ring 0, segment de données lisible et inscriptible.
- **`0xFA` (User Code) :** `11111010b` → présent, Ring 3, code exécutable et lisible.
- **`0xF2` (User Data/Stack) :** `11110010b` → présent, Ring 3, segment de données lisible et inscriptible.

#### D. La granularité et les flags (`granularity`) - octet 6

Cet octet est divisé en deux parties : les 4 bits de poids fort contiennent les flags et les 4 bits de poids faible contiennent la limite.

```text
Bit :  7   6   5   4       3..0
      ┌───┬───┬───┬───┬────────────┐
      │ G │D/B│ L │AVL│ Limit 16..19│
      └───┴───┴───┴───┴────────────┘
```

| Bit(s) | Nom | Signification / valeurs |
| :---: | :--- | :--- |
| 7 | `G` (Granularity) | `0` : limite mesurée en octets. `1` : limite mesurée en pages de 4 Ko. |
| 6 | `D/B` (Default operation size) | `1` : mode 32 bits. `0` : mode 16 bits. |
| 5 | `L` (64-bit code segment) | Réservé au mode 64 bits. Doit être à `0` en mode 32 bits. |
| 4 | `AVL` (Available) | Libre d'utilisation pour le développeur, laissé à `0`. |
| 3-0 | `Limit 16..19` | Bits 16 à 19 de la limite du segment. |

##### Pourquoi `0xCF` pour la granularité dans KFS-2 ?

`0xCF` en binaire vaut `11001111b` :

- `1` (`G`) : granularité de 4 Ko.
- `1` (`D/B`) : instructions 32 bits.
- `0` (`L`) : mode 32 bits pur, pas de mode 64 bits.
- `0` (`AVL`) : champ non utilisé.
- `1111` (`Limit 16..19`) : les 4 derniers bits de la limite `0xFFFFF`.

Avec `Limit = 0xFFFFF` et `Granularity = 0xCF`, la taille totale du segment devient :

$$
\text{0xFFFFF} \times 4096\text{ octets} \approx 4\text{ Go}
$$

Le segment couvre ainsi toute la mémoire disponible en mode 32 bits.

## 7. Réassemblage dans `gdt_set_gate`

Voici ce que fait mathématiquement la fonction `gdt_set_gate()` :

```c
void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                  uint8_t access, uint8_t gran) {
    // 1. Découpage de l'adresse de base 32 bits
    gdt[num].base_low    = (base & 0xFFFF);        // Prend les 16 bits de poids faible
    gdt[num].base_middle = (base >> 16) & 0xFF;    // Prend les bits 16 à 23
    gdt[num].base_high   = (base >> 24) & 0xFF;    // Prend les bits 24 à 31

    // 2. Découpage de la limite (qui vaut 0xFFFFFFFF en entrée)
    gdt[num].limit_low   = (limit & 0xFFFF);       // Met les 16 premiers 'F' (0xFFFF)
    gdt[num].granularity = (limit >> 16) & 0x0F;   // Prend les bits 16..19 (0x0F)

    // 3. Application des flags et de l'accès
    gdt[num].granularity |= (gran & 0xF0);          // Fusionne les flags (0xC0) avec la limite
    gdt[num].access      = access;                 // Applique l'octet d'accès (ex. 0x9A)
}
```


## 8. Documentation technique : remontée de pile (`print_stack`)

### 1. Objectif

En développement de noyau bare-metal, il n'existe pas de débogueur interactif ou de mécanisme standard d'affichage d'erreurs (comme un Segmentation Fault détaillé). La fonction `print_stack` résout ce problème en effectuant une remontée de pile (Stack Unwinding ou Backtrace). Elle permet d'afficher la chaîne des appels de fonctions (call stack) et l'état des cadres de pile (stack frames) à un instant précis du noyau.

### 2. Analyse de l'organisation de la pile en x86 (32 bits)

Sur l'architecture x86, la pile grandit vers les adresses basses. À chaque appel de fonction via l'instruction `call`, ainsi que lors du préambule standard généré par le compilateur C, la mémoire est organisée de la manière suivante :

```text
       Adresse Haute (Fond de pile - stack_top)
      +------------------------------+
      | ... Arguments de la fonction |
      +------------------------------+
      | Adresse de retour (EIP)      |  <-- frame[1]
EBP ->+------------------------------+
      | Ancien EBP (Frame parent)    |  <-- frame[0]
      +------------------------------+
      | Variables locales            |
ESP ->+------------------------------+
       Adresse Basse (Sommet de pile - stack_bottom)
```

Chaque cadre de pile (stack frame) est structuré autour du registre EBP (Base Pointer) :

- `EBP + 0 (frame[0])` : Contient l'adresse du registre EBP de la fonction appelante (créant ainsi une liste simplement chaînée de cadres).
- `EBP + 4 (frame[1])` : Contient l'adresse de retour (EIP), c'est-à-dire l'adresse de l'instruction assembleur qui suit immédiatement l'appel de la fonction.

### 3. Fonctionnement de l'algorithme

L'algorithme de `print_stack` parcourt cette liste chaînée d'EBP jusqu'au sommet de la pile initiale du noyau :

1. Récupération du pointeur de départ :
   L'instruction assembleur inline `movl %%ebp, %0` extrait l'adresse courante du registre `%ebp` pour l'assigner à notre pointeur C `frame`.

2. Vérification des bornes de la pile (Sanity Check) :
   Pour éviter tout accès mémoire invalide ou une boucle infinie due à une pile corrompue, l'adresse du cadre courant `frame_address` est validée contre les bornes physiques de la pile du noyau (`stack_bottom` et `stack_top`) définies dans la séquence de boot.

3. Extraction et affichage :
   À chaque étape de la boucle, la fonction affiche :

   - Le numéro du cadre (`frame_number`).
   - L'adresse du cadre courant (`frame_address`).
   - L'adresse de retour (`frame[1]`), indiquant la position exacte dans le binaire ELF.

4. Validation de cohérence :
   En architecture x86, l'EBP parent (`frame[0]`) doit impérativement pointer vers une adresse plus haute en mémoire que l'EBP courant (`frame[0] > frame_address`). Si ce n'est pas le cas, la pile est corrompue ou arrivée au bout de sa traçabilité.

5. Chaînage :
   Le pointeur `frame` est mis à jour vers `(uint32_t *)frame[0]` pour inspecter le cadre de la fonction précédente.

### 4. Exemple de sortie et interprétation

```text
Kernel stack trace:
  #0x00000000 frame=0x00105fe8 return=0x001002ee
  #0x00000001 frame=0x00106008 return=0x001002fd
```

Lecture du Log :

- Frame #0 : Représente le cadre de la fonction `print_stack()`. L'adresse de retour `0x001002ee` indique où le processeur reviendra une fois la fonction achevée.
- Frame #1 : Représente la fonction appelante (par exemple `kmain()`). L'adresse du cadre `0x00106008` est plus élevée que la précédente (`0x00105fe8`), confirmant la remontée vers les adresses hautes de la pile.

## 9. Explication détaillée du code : `print_stack()`

### 1. Déclaration des variables de contrôle

```c
uint32_t *frame;
uint32_t frame_address;
uint32_t stack_start = (uint32_t)stack_bottom;
uint32_t stack_end = (uint32_t)stack_top;
uint32_t frame_number = 0;
```

- `frame` : Pointeur de 32 bits qui va pointer sur l'adresse du cadre de pile (EBP) actuellement analysé.
- `frame_address` : Conversion de `frame` en valeur numérique (`uint32_t`) pour pouvoir effectuer facilement des comparaisons logiques d'adresses mémoire.
- `stack_start` et `stack_end` : Variables récupérant les adresses limites de la pile allouée au noyau (définies dans l'assembleur de boot via `stack_bottom` et `stack_top`).
- `frame_number` : Compteur d'itérations pour numéroter les cadres affichés.

### 2. Capture du registre EBP

```c
__asm__ volatile ("movl %%ebp, %0" : "=r"(frame));
```

Rôle : Cette instruction d'assembleur inline copie la valeur courante du registre processeur `%ebp` dans la variable C `frame`.

Détail syntaxique (GAS) : `movl %%ebp, %0` déplace la valeur 32-bit de `%ebp` vers le premier opérande de sortie (`%0`), lié à `frame` par la contrainte `"=r"` (assignation à un registre général).

### 3. La boucle d'exploration et contrôle de validité

```c
while (frame_number < 32) {
    frame_address = (uint32_t)frame;
    if (frame_address < stack_start || frame_address + 8 > stack_end) {
        printk("  invalid frame at ");
        printk_hex(frame_address);
        printk("\n");
        return;
    }
```

- Limite de sécurité : La boucle est bridée à 32 itérations pour parer à tout risque de boucle infinie.
- Sanity Check (Sécurité mémoire) : On vérifie que `frame_address` se situe bien à l'intérieur des limites de la pile du noyau (`stack_start <= frame_address` et `frame_address + 8 <= stack_end`).
- Si le pointeur sort de ces bornes (pile corrompue ou atteinte de la fin du segment), un message d'erreur indique l'adresse invalide et la fonction interrompt immédiatement son exécution (`return`).

### 4. Extraction et affichage du cadre

```c
    printk("  #");
    printk_hex(frame_number);
    printk(" frame=");
    printk_hex(frame_address);
    printk(" return=");
    printk_hex(frame[1]);
    printk("\n");
```

- `frame[0]` : Accède aux 4 premiers octets pointés par `EBP` (l'adresse sauvegardée de l'EBP parent).
- `frame[1]` : Accède aux 4 octets suivants (`EBP + 4`), qui contiennent l'adresse de retour (`EIP`).
- Affichage : La fonction imprime le numéro du cadre, son adresse en mémoire, et l'adresse de retour `EIP` sous format hexadécimal.

### 5. Détection de corruption et remontée du chaînage

```c
    if (frame[0] <= frame_address) {
        return;
    }
    frame = (uint32_t *)frame[0];
    frame_number++;
}
```

- Test de monotonie : En x86, la pile grandit vers les adresses basses. L'EBP de la fonction appelante (`frame[0]`) doit obligatoirement être situé à une adresse strictement supérieure à l'EBP de la fonction appelée (`frame_address`). Si `frame[0] <= frame_address`, la structure de la pile est invalide ou l'on a atteint le fond de la pile initialisé à `0`.
- Avancement : `frame = (uint32_t *)frame[0]` fait sauter notre pointeur vers le cadre de la fonction précédente pour poursuivre la remontée.

### 6. Condition de troncature

```c
printk("  ... stack trace truncated\n");
```

Si la boucle atteint 32 itérations sans rencontrer de cadre invalide ou de fin de pile, ce message signale que la trace a été volontairement écourtée.

## 10. Documentation technique : chargement de la GDT (`gdt_flush.s`)

### 1. Objectif

En C, nous pouvons construire la structure de la GDT en mémoire RAM, mais le processeur x86 ne sait pas automatiquement qu'elle existe. Pour qu'elle devienne active, il faut exécuter l'instruction assembleur privilégiée `lgdt` (Load Global Descriptor Table) puis recharger immédiatement tous les registres de segments (`CS`, `DS`, `ES`, `FS`, `GS`, `SS`).

### 2. Code assembleur annoté

Extrait de code :

```asm
global gdt_flush
.type gdt_flush, @function

gdt_flush:
    /* 1. Récupération du pointeur GDT et chargement matériel */
    movl 4(%esp), %eax      /* Récupère l'adresse de gdt_ptr transmise sur la pile */
    lgdt (%eax)             /* Charge la structure pointée par EAX dans le registre GDTR */

    /* 2. Mise à jour des registres de segment de données (Kernel Data = 0x10) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 3. Far jump pour recharger le registre de segment de code CS (Kernel Code = 0x08) */
    ljmp $0x08, $.reload_cs

.reload_cs:
    ret
```

### 3. Explication détaillée ligne par ligne

#### Étape 1 : Chargement du registre GDTR

Extrait de code :

```asm
movl 4(%esp), %eax
lgdt (%eax)
```

Pourquoi `4(%esp)` ? En C (convention d'appel cdecl), lorsqu'on appelle `gdt_flush(&gp)`, l'adresse de `gp` est empilée juste avant le saut dans la fonction. Au sommet de la pile (`0(%esp)`), se trouve l'adresse de retour (`EIP`). Le paramètre `&gp` se situe donc immédiatement au-dessus, à l'adresse `4(%esp)`.

Pourquoi `lgdt (%eax)` ? L'instruction `lgdt` attend en paramètre l'adresse mémoire d'une structure de 6 octets (`gdt_ptr`). Elle lit les 2 premiers octets (la limite) et les 4 suivants (l'adresse de base), puis copie ces valeurs directement dans le registre processeur `GDTR` (Global Descriptor Table Register).

#### Étape 2 : Rechargement des segments de données

Extrait de code :

```asm
movw $0x10, %ax
movw %ax, %ds
movw %ax, %es
movw %ax, %fs
movw %ax, %gs
movw %ax, %ss
```

Pourquoi réécrire dans ces registres ? Même si la GDT est chargée matériellement par `lgdt`, le processeur continue d'utiliser en interne les anciens sélecteurs de segment configurés par GRUB. Pour appliquer notre nouvelle GDT, il faut forcer le rechargement de chaque registre de segment.

Pourquoi la valeur `0x10` ? En x86, la valeur chargée dans un registre de segment est un Sélecteur de Segment. Les 13 bits de poids fort représentent l'index dans la GDT :

- Index `0` (`0x00`) = Segment Nul.
- Index `1` (`0x08`) = Code Kernel ($1 \times 8 = 8 = 0x08$).
- Index `2` (`0x10`) = Data Kernel ($2 \times 8 = 16 = 0x10$).

Charger `0x10` indique donc au CPU d'associer la mémoire de données et de pile au deuxième segment de notre GDT.

#### Étape 3 : Le saut distant (Far Jump) pour le segment de code (`CS`)

Extrait de code :

```asm
ljmp $0x08, $.reload_cs
.reload_cs:
    ret
```

Pourquoi ne peut-on pas faire `movw $0x08, %cs` ? L'architecture x86 interdit la modification directe du registre `CS` (Code Segment) avec une instruction `mov`. `CS` détermine le privilège d'exécution actuel du processeur (Ring 0 ou Ring 3) et l'adresse des instructions lues par le CPU.

Pourquoi utiliser `ljmp` ? La seule façon de mettre à jour `CS` est d'effectuer un saut distant (Far Jump). L'instruction `ljmp $0x08, $.reload_cs` réalise deux actions simultanées :

- Elle recharge le registre `CS` avec le sélecteur `0x08` (Kernel Code).
- Elle force le processeur à vider le pipeline d'instructions préchargées et à continuer l'exécution au label `.reload_cs`.

Le `ret` final : Maintenant que `CS`, `DS`, `ES`, `FS`, `GS` et `SS` pointent tous sur notre propre GDT à l'adresse `0x00000800`, la fonction peut dépiler l'adresse de retour et rendre la main au code C de `init_gdt()`.