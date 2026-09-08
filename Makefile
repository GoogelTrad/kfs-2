NAME = mykernel.bin
ISO_NAME = mykernel.iso

CC = gcc
AS = gcc
LDFLAGS = -m32 -T linker.ld -nostdlib -nodefaultlibs
CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -std=gnu99 -fno-builtin -fno-exceptions -fno-stack-protector #-fno-rtti
ASFLAGS = -m32 -c

SRC_DIR = src
OBJ_DIR = obj
ISO_DIR = iso_root

OBJS = $(OBJ_DIR)/kernel.o $(OBJ_DIR)/boot.o $(OBJ_DIR)/helpers.o

all: $(NAME)

$(NAME): $(OBJ_DIR) $(OBJS)
	$(CC) $(LDFLAGS) -o $(NAME) $(OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.s
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# NEW RULE: Packages the kernel into a real bootable GRUB ISO image
iso: clean $(NAME)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(NAME) $(ISO_DIR)/boot/
	cp $(ISO_DIR)/boot/grub/grub.cfg $(ISO_DIR)/boot/grub/2>/dev/null || true
	grub-file --is-x86-multiboot $(ISO_DIR)/boot/$(NAME)
	grub-mkrescue -o $(ISO_NAME) $(ISO_DIR)
	rm -f $(NAME)

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(ISO_DIR)/boot/$(NAME)

restart: clean iso

fclean: clean
	rm -f $(NAME)
	rm -f $(ISO_NAME)

re: fclean all iso

.PHONY: all clean fclean re iso