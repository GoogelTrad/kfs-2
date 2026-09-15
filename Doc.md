# Documentation du projet KFS-2

## 1. Vue d'ensemble du projet

KFS-2 est un petit noyau bare-metal en mode 32 bits x86. Le but est de démarrer un système depuis le bootloader GRUB, de configurer le matériel de base, puis d'afficher un message texte dans une console VGA sans dépendre d'un système d'exploitation complet.

Le projet montre les bases d'un noyau minimal :

- initialisation du pile de démarrage,
- chargement d'une GDT (Global Descriptor Table),
- activation des segments mémoire du processeur,
- écriture sur la mémoire vidéo en mode texte,
- diagnostics de pile via une remontée de pile.

Ce document explique les concepts importants du projet, donne un exemple concret, puis détaille le code correspondant dans les fichiers sources.

> Cette documentation ne couvre pas le Makefile, le Dockerfile ni le script de lancement. Le focus est sur le comportement du noyau lui-même et les notions système qu'il met en œuvre.

---

## 2. Le noyau bare-metal : notion et objectif

### Concept

Un noyau bare-metal est un noyau exécuté directement sur le matériel, sans système d'exploitation hôte. Il n'y a ni libc, ni OS pour fournir des services, ni console Linux au-dessus. Le code tourne en mode privilégié, sur le CPU, et doit gérer lui-même l'initiation du matériel.

Cela implique que le développeur doit contrôler :

- la pile,
- les registres de segments,
- la mémoire vidéo,
- les interruptions et la sécurité du processeur,
- l'organisation de la mémoire.

### Exemple concret

Quand une machine démarre avec GRUB, le bootloader charge le noyau en mémoire et transfère le contrôle au point d'entrée `_start`. À partir de là, il n'y a plus de couche logicielle qui vient aider. Le noyau doit donc créer sa propre base de fonctionnement.

### Code correspondant

Le point d'entrée est dans le fichier [src/boot.s](src/boot.s) :

```asm
.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp
    call kernel_main

    cli
1:  hlt
    jmp 1b
```

### Explication

- `_start` est le point d'entrée du noyau.
- `mov $stack_top, %esp` initialise la pile du noyau.
- `call kernel_main` passe la main au code C.
- `cli` désactive les interruptions.
- `hlt` puis `jmp 1b` met le processeur en attente infinie si le noyau devait retourner.

C'est la base d'un système minimal : le CPU démarre avec une pile, puis entre dans le code du noyau.

---

## 3. Le bootloader et la structure Multiboot

### Concept

GRUB respecte une convention appelée Multiboot. Elle permet au bootloader de connaître le point d'entrée du noyau et de lui donner une configuration minimaliste. Le noyau déclare un header Multiboot pour être reconnu.

### Exemple concret

Le bootloader sait alors que le noyau commence à `_start` et qu'il doit aligner les modules chargés sur les pages mémoire.

### Code correspondant

Dans [src/boot.s](src/boot.s) :

```asm
# Constants for the Multiboot Standard
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
```

### Explication

- `MAGIC` est la signature attendue par GRUB.
- `FLAGS` indique les options de chargement.
- `CHECKSUM` valide le header.
- le bloc `.multiboot` est la zone que GRUB lit pour démarrer le noyau.

Sans ce header, le noyau ne serait pas reconnu et le bootloader refuserait de l'exécuter.

---

## 4. La pile du noyau : pourquoi est-elle essentielle ?

### Concept

La pile est une zone mémoire utilisée pour stocker :

- les adresses de retour des fonctions,
- les variables locales,
- les arguments,
- l'état de l'exécution.

En mode 32 bits x86, la pile grandit vers des adresses basses. Cela signifie que chaque appel de fonction pousse des données en mémoire puis dépile en revenant.

### Exemple concret

Quand `kernel_main` appelle une fonction comme `print_stack`, le processeur stocke l'adresse de retour sur la pile. Ensuite, la fonction peut lire ses arguments et ses variables locales à partir de `EBP` et `ESP`.

### Code correspondant

Dans [src/boot.s](src/boot.s) :

```asm
.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:
```

### Explication

- `stack_bottom` et `stack_top` délimitent la zone allouée pour la pile.
- `16384` octets = 16 Ko.
- `stack_top` est utilisé pour initialiser le registre `%esp`.

Cette pile sert ensuite à toutes les fonctions du noyau, notamment pour les appels C et la remontée de pile.

---

## 5. Le mode texte VGA : écrire à l'écran sans pilote graphique

### Concept

Le mode texte VGA est un mode d'affichage très simple où l'écran est vu comme une grille de 80 colonnes sur 25 lignes. Chaque cellule contient un caractère ASCII et un attribut de couleur.

Cette technique est très utile dans un noyau minimal car elle ne demande pas de pilote graphique complet.

### Exemple concret

L'écran affiche par exemple :

```text
Welcome to KFS-1 from scratch!
System initialized successfully...
```

### Code correspondant

Dans [src/kernel.h](src/kernel.h) :

```c
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000
```

Puis dans [src/kernel.c](src/kernel.c) :

```c
terminal_buffer = (uint16_t*) VGA_ADDRESS;
```

### Explication

- `0xB8000` est l'adresse mémoire du framebuffer VGA texte.
- chaque cellule d'écran est représentée par 2 octets :
  - le premier = caractère ASCII,
  - le second = attribut de couleur.

Le noyau écrit donc directement dans la mémoire vidéo. C'est une méthode très simple et très efficace pour un noyau minimal.

---

## 6. La console du noyau : gestion du terminal

### Concept

Un terminal dans un noyau est une abstraction pour écrire des caractères à l'écran. Il faut gérer :

- les retours à la ligne,
- le défilement de l'écran,
- la position du curseur,
- la couleur du texte.

### Exemple concret

Quand le noyau écrit plusieurs lignes, la console remonte la vue au besoin. Si la dernière ligne est remplie, on fait défiler les lignes vers le haut.

### Code correspondant

Dans [src/kernel.c](src/kernel.c) :

```c
void terminal_putchar(char c) 
{
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
        return;
    }

    const int index = terminal_row * VGA_WIDTH + terminal_column;
    terminal_buffer[index] = c | (terminal_color << 8);
```

### Explication

- si le caractère est `\n`, on passe à la ligne,
- si la fin de l'écran est atteinte, on appelle `terminal_scroll()`,
- sinon, on écrit le caractère dans la cellule correspondante,
- `terminal_color << 8` place la couleur dans le second octet du mot VGA.

La console affiche donc des messages du noyau sans dépendre d'un système de fichiers ou d'un driver standard.

---

## 7. La GDT : notion fondamentale du mode protégé

### Concept

La GDT (Global Descriptor Table) est une table de descripteurs de segments. Elle décrit au processeur :

- où commence un segment,
- quelle est sa taille,
- quels droits d'accès il possède,
- s'il est de code ou de données,
- à quel niveau de privilège il appartient.

En mode protégé x86, le CPU n'utilise pas la mémoire brute sans règles. Il faut lui donner des segments autorisés pour exécuter du code et accéder à la mémoire.

### Exemple concret

Si le noyau veut exécuter du code en Ring 0 et utiliser des données en mémoire, il crée des segments comme :

- `Kernel Code` : code exécutable,
- `Kernel Data` : données lisibles/écrasables,
- `User Code` : réservé à un futur programme utilisateur,
- `User Data` : données utilisateur.

### Code correspondant

Dans [src/gdt.c](src/gdt.c) :

```c
static struct gdt_entry *gdt = (struct gdt_entry *)0x00000800;
static struct gdt_ptr gp;
```

Puis :

```c
gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
```

### Explication

- `gdt` est placée à l'adresse fixe `0x00000800`.
- `gdt_ptr` est la structure que le processeur reçoit via `lgdt`.
- `gdt_set_gate` découpe la base et la limite en champs 32 bits / 20 bits.
- `0x9A` correspond à un segment de code kernel.
- `0x92` correspond à un segment de données kernel.
- `0xCF` donne la granularité et le mode 32 bits.

Cette configuration permet d'avoir un modèle mémoire plat où les segments couvrent presque toute la mémoire RAM.

---

## 8. Pourquoi la GDT est-elle obligatoire ?

### Concept

Sans GDT, le CPU n'a aucun moyen de savoir ce qui est du code, des données, ou quel segment est autorisé. Cela bloque l'exécution normale du noyau.

Le bootloader GRUB fournit une GDT temporaire, mais le noyau doit créer la sienne pour :

- éviter d'utiliser une table dont l'emplacement n'est pas contrôlé,
- respecter une adresse mémoire fixe exigée par le sujet,
- garantir un environnement stable avant toute exécution avancée.

### Exemple concret

Si un noyau s'appuie sur une GDT de GRUB, et que cette table est écrasée, le CPU peut lever une erreur grave et redémarrer. En revanche, une GDT installée à `0x00000800` reste organisée et stable.

### Code correspondant

Dans [src/gdt.c](src/gdt.c) :

```c
void init_gdt(void) {
    gp.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gp.base  = (uint32_t)gdt;

    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_gate(5, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    gdt_flush((uint32_t)&gp);
}
```

### Explication

- 7 descripteurs sont créés : 1 null + 6 utiles.
- `gp.limit` indique la taille de la GDT.
- `gp.base` donne l'adresse physique de la table.
- `gdt_flush` charge le CPU avec cette table.

C'est le moment où le processeur passe d'un état non configuré à un état de segmentation valide.

---

## 9. La structure de descripteur GDT

### Concept

Un descripteur GDT est un bloc de 8 octets. C'est la structure minimale que le processeur attend pour savoir comment un segment est défini.

Chaque descripteur contient :

- la base du segment,
- la limite du segment,
- les droits d'accès,
- les flags de granularité.

### Exemple concret

Voici la structure utilisée dans le projet :

Dans [src/gdt.h](src/gdt.h) (selon le pattern du projet), on trouve la forme d'un descripteur :

```c
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));
```

### Explication

- `__attribute__((packed))` empêche l'ajout de padding.
- on veut exactement 8 octets, pas plus.
- le processeur lit cette structure en binaire et construit le segment de mémoire associé.

Cela montre un point essentiel d'architecture x86 : bien que le compilateur gère la mémoire de manière logique, le CPU attend une structure précise et compacte.

---

## 10. La fonction `gdt_set_gate` : assembler un descripteur

### Concept

La fonction `gdt_set_gate` prend des informations logiques (base, limite, accès, granularité) et les écrit dans les bons champs d'un descripteur GDT.

### Exemple concret

Pour créer le segment de code noyau, on appelle :

```c
gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
```

### Code correspondant

Dans [src/gdt.c](src/gdt.c) :

```c
static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;

    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access      = access;
}
```

### Explication

- `base` est découpé en 3 morceaux : faible, moyen, haut.
- `limit` est découpé en 16 bits + 4 bits de granularité.
- `gran` ajoute les flags de taille et de mode.
- `access` donne les droits d'accès.

C'est un exemple typique de transformation d'un concept logique en représentation binaire compatible avec le CPU.

---

## 11. `gdt_flush` : activer la GDT au niveau matériel

### Concept

Construire la GDT en mémoire ne suffit pas. Le processeur doit être informé de son emplacement via le registre GDTR.

C'est le rôle de `lgdt` (Load Global Descriptor Table).

### Exemple concret

Le noyau construit la table, puis appelle :

```c
gdt_flush((uint32_t)&gp);
```

### Code correspondant

Le fichier assembleur correspondant se trouve dans le dossier source, nommé `gdt_flush.s` selon la convention du projet. Le code est le suivant :

```asm
global gdt_flush

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.reload_cs

.reload_cs:
    ret
```

### Explication

- `lgdt [eax]` charge la GDT dans le registre GDTR du CPU.
- `mov ax, 0x10` charge le sélecteur du segment de données kernel.
- `mov ds, ax`, `mov es, ax`, ... rechargent les segments de données.
- `jmp 0x08:.reload_cs` force le CPU à recharger le segment de code.

Sans cette étape, la GDT construite en mémoire serait simplement inactive.

---

## 12. La remontée de pile : comprendre le call stack

### Concept

Une remontée de pile, ou backtrace, permet de traverser la pile des appels de fonctions et de retrouver le chemin d'exécution actuel.

En x86, chaque cadre de pile est souvent lié au registre `EBP` (Base Pointer).

### Exemple concret

Si `kernel_main` appelle `print_stack`, puis `terminal_writestring`, etc., il est possible de voir dans quel cadre on se trouve et quelle fonction a appelé l'autre.

La sortie d'un backtrace ressemble à :

```text
Kernel stack trace:
  #0x00000000 frame=0x00105fe8 return=0x001002ee
  #0x00000001 frame=0x00106008 return=0x001002fd
```

### Code correspondant

Dans [src/helpers.c](src/helpers.c) :

```c
void print_stack(void)
{
    uint32_t *frame;
    uint32_t frame_address;
    uint32_t stack_start = (uint32_t)stack_bottom;
    uint32_t stack_end = (uint32_t)stack_top;
    uint32_t frame_number = 0;

    __asm__ volatile ("movl %%ebp, %0" : "=r"(frame));
    printk("Kernel stack trace:\n");

    while (frame_number < 32) {
        frame_address = (uint32_t)frame;
        if (frame_address < stack_start || frame_address + 8 > stack_end) {
            printk("  invalid frame at ");
            printk_hex(frame_address);
            printk("\n");
            return;
        }

        printk("  #");
        printk_hex(frame_number);
        printk(" frame=");
        printk_hex(frame_address);
        printk(" return=");
        printk_hex(frame[1]);
        printk("\n");

        if (frame[0] <= frame_address) {
            return;
        }
        frame = (uint32_t *)frame[0];
        frame_number++;
    }

    printk("  ... stack trace truncated\n");
}
```

### Explication

- `movl %%ebp, %0` récupère le pointeur du cadre courant.
- `frame[0]` correspond à l'ancien `EBP`.
- `frame[1]` correspond à l'adresse de retour (`EIP`).
- on vérifie que la pile est valide avant de lire des valeurs,
- on remonte la chaîne jusqu'à la fin de la pile ou jusqu'à une corruption détectée.

La remonte de pile est un outil crucial pour déboguer un noyau car il n'existe pas de système d'exploitation standard pour nous donner des traces d’erreur.

---

## 13. La fonction d'aide `printk` et le format hexadécimal

### Concept

Le noyau n'a pas de printf standard. Il doit donc construire ses propres outils d'affichage. Dans ce projet, il y a un petit mécanisme de sortie texte et un formatteur hexadécimal.

### Exemple concret

Quand on veut afficher une adresse comme `0x00105fe8`, il faut transformer la valeur numérique en chaîne de caractères.

### Code correspondant

Dans [src/helpers.c](src/helpers.c) :

```c
static void printk_hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[11];

    buffer[0] = '0';
    buffer[1] = 'x';
    for (uint32_t i = 0; i < 8; i++) {
        buffer[2 + i] = digits[(value >> (28 - i * 4)) & 0xf];
    }
    buffer[10] = '\0';
    printk(buffer);
}
```

### Explication

- `digits` contient les symboles hexadécimaux.
- on prend des groupes de 4 bits à la fois : `(value >> (28 - i*4)) & 0xf`.
- on reconstruit une chaîne avec le préfixe `0x`.

C'est la base de l'affichage de valeurs mémoire, très utile en débogage système.

---

## 14. Le point d'entrée C : `kernel_main`

### Concept

Une fois la pile initialisée et la GDT chargée, le noyau peut enfin exécuter du code C. C'est là que l'initialisation du système se passe.

### Exemple concret

Le noyau affiche un message de bienvenue, initialise la console, configure la GDT, puis exécute une trace de pile.

### Code correspondant

Dans [src/kernel.c](src/kernel.c) :

```c
void kernel_main(void) 
{
    terminal_initialize();
    init_gdt();

    terminal_writestring("Welcome to KFS-1 from scratch!\n");
    terminal_writestring("System initialized successfully...\n");
    terminal_writestring("-----------------------------------\n");
    terminal_writestring("Booting core context...\n");
    terminal_writestring("Done.\n\n");

    terminal_writestring("42\n");
    print_stack();

    while (1) {
        // Halt state safely
    }
}
```

### Explication

- `terminal_initialize()` configure la console VGA.
- `init_gdt()` active la GDT du noyau.
- `terminal_writestring()` affiche le message de démarrage.
- `print_stack()` démontre le fonctionnement de la pile.
- la boucle infinie maintient le processeur en état de repos.

C'est une bonne représentation du démarrage d'un noyau minimal : initialisation puis exécution stable.

---

## 15. Conclusion

KFS-2 est un noyau minimal qui démontre les principes fondamentaux du développement système sur x86 en mode protégé 32 bits :

- bootloader / Multiboot,
- pile,
- mémoire vidéo VGA,
- tableaux de segments via la GDT,
- rechargement de registres de segment,
- remontée de pile pour le débogage,
- code C exécuté directement sur le matériel.

Même si le projet reste simplifié, il introduit les idées essentielles du fonctionnement d'un noyau réel : la mémoire, la sécurité du processeur, la gestion des segments et le contrôle total du matériel.

Le projet montre qu'un noyau ne part pas d'un système complet, mais d'un ensemble de briques très simples qui s'assemblent pour donner un environnement d'exécution minimal, stable et contrôlé.
