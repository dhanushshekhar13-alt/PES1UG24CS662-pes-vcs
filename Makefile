CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcrypto

# Main targets
all: pes

pes: object.o tree.o index.o commit.o pes.o
	$(CC) -o pes $^ $(LDFLAGS)

# Individual modules
object.o: object.c pes.h
	$(CC) $(CFLAGS) -c object.c

tree.o: tree.c tree.h pes.h index.h
	$(CC) $(CFLAGS) -c tree.c

index.o: index.c index.h pes.h
	$(CC) $(CFLAGS) -c index.c

commit.o: commit.c commit.h pes.h tree.h
	$(CC) $(CFLAGS) -c commit.c

pes.o: pes.c pes.h tree.h index.h commit.h
	$(CC) $(CFLAGS) -c pes.c

# Clean up
clean:
	rm -f pes *.o
	rm -rf .pes	@echo "=== Running Phase 1 tests ==="
	./test_objects
	@echo ""
	@echo "=== Running Phase 2 tests ==="
	./test_tree

test-integration: pes
	@echo "=== Running integration tests ==="
	bash test_sequence.sh
