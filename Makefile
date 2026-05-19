CXX= g++
CXXFLAGS= -std=c++17 

INCLUDE= $(shell sdl2-config --cflags)
LIB= $(shell sdl2-config --libs) -lSDL2_image -lSDL2_ttf

SRCDIR= src
OBJDIR= obj
BINDIR= bin

OBJS= $(addprefix $(OBJDIR)/, main.o app.o)
EXEC= $(addprefix $(BINDIR)/, os-gui)


mkdirs:= $(shell mkdir -p $(OBJDIR) $(BINDIR))


all: $(EXEC)

$(EXEC): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIB)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $< $(INCLUDE)


clean:
	rm -f $(OBJS) $(EXEC)
