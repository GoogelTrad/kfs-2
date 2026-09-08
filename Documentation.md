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
