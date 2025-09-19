# Makefile for Buffered Logger
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread -g
CXXFLAGS_DEBUG = -std=c++17 -Wall -Wextra -O0 -pthread -g -DDEBUG -fsanitize=thread
CXXFLAGS_RELEASE = -std=c++17 -Wall -Wextra -O3 -pthread -DNDEBUG

# Source files
SRCS = buffered_logger.cpp
TEST_SRCS = test_buffered_logger.cpp
EXAMPLE_SRCS = example_usage.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
EXAMPLE_OBJS = $(EXAMPLE_SRCS:.cpp=.o)

# Executables
TEST_EXEC = test_logger
EXAMPLE_EXEC = example_logger

# Targets
all: $(TEST_EXEC) $(EXAMPLE_EXEC)

test: $(TEST_EXEC)
	@echo "Running tests..."
	./$(TEST_EXEC)

example: $(EXAMPLE_EXEC)
	@echo "Running example..."
	./$(EXAMPLE_EXEC)

$(TEST_EXEC): $(OBJS) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(EXAMPLE_EXEC): $(OBJS) $(EXAMPLE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp buffered_logger.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Debug build
debug: CXXFLAGS = $(CXXFLAGS_DEBUG)
debug: clean $(TEST_EXEC)
	@echo "Debug build complete with ThreadSanitizer"

# Release build
release: CXXFLAGS = $(CXXFLAGS_RELEASE)
release: clean $(TEST_EXEC) $(EXAMPLE_EXEC)
	@echo "Release build complete"

# Static library
lib: $(OBJS)
	ar rcs libbuffered_logger.a $(OBJS)
	@echo "Static library created: libbuffered_logger.a"

# Shared library
shared: CXXFLAGS += -fPIC
shared: $(OBJS)
	$(CXX) -shared -o libbuffered_logger.so $(OBJS)
	@echo "Shared library created: libbuffered_logger.so"

# Performance profiling build
profile: CXXFLAGS += -pg
profile: clean $(TEST_EXEC)
	@echo "Profile build complete. Run './$(TEST_EXEC)' then 'gprof $(TEST_EXEC) gmon.out'"

# Memory check with valgrind
memcheck: $(TEST_EXEC)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TEST_EXEC)

# Thread safety check
threadcheck: debug
	./$(TEST_EXEC)

# Clean
clean:
	rm -f $(OBJS) $(TEST_OBJS) $(EXAMPLE_OBJS) $(TEST_EXEC) $(EXAMPLE_EXEC)
	rm -f *.log
	rm -f libbuffered_logger.a libbuffered_logger.so
	rm -f gmon.out
	@echo "Clean complete"

# Install (example - adjust paths as needed)
PREFIX = /usr/local
install: lib
	install -d $(PREFIX)/include
	install -d $(PREFIX)/lib
	install -m 644 buffered_logger.h $(PREFIX)/include/
	install -m 644 libbuffered_logger.a $(PREFIX)/lib/
	@echo "Installation complete"

# Uninstall
uninstall:
	rm -f $(PREFIX)/include/buffered_logger.h
	rm -f $(PREFIX)/lib/libbuffered_logger.a
	@echo "Uninstallation complete"

.PHONY: all test example debug release lib shared profile memcheck threadcheck clean install uninstall