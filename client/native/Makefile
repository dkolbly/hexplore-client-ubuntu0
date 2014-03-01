OUT=.out
SDL2_CONFIG=sdl2-config
PNG_CONFIG=libpng-config
# GLFW_CONFIG=/home/donovan/lib/glfw3/lib/pkgconfig/glfw3.pc

CFLAGS=-I../../server/src/.out -I../.. -std=c++11 -g -Wall \
	`$(SDL2_CONFIG) --cflags` \
	`$(PNG_CONFIG) --cflags`
#`pkg-config --cflags $(GLFW_CONFIG)` 

#SDL_TTF_LFLAGS=-lSDL2_ttf

LFLAGS=-L../../hexcom -lhexcom `$(SDL2_CONFIG) --libs`  $(SDL_TTF_LFLAGS) \
	`$(PNG_CONFIG) --ldflags` \
	-lGLU -lGL -lprotobuf -ljsoncpp -lz -lpthread
#`pkg-config --libs $(GLFW_CONFIG)`

CPP_SOURCES=main.cpp mesh.cpp world.cpp connection.cpp text_ui.cpp \
	sound.cpp skymap.cpp clientoptions.cpp

SERVER_OUT_DIR=../../server/src/.out

PROTO_SOURCES=\
   $(SERVER_OUT_DIR)/wire/hello.pb.cc \
   $(SERVER_OUT_DIR)/wire/model.pb.cc \
   $(SERVER_OUT_DIR)/wire/entity.pb.cc \
   $(SERVER_OUT_DIR)/wire/terrain.pb.cc \
   $(SERVER_OUT_DIR)/wire/major.pb.cc

OBJ_FILES=$(patsubst %.cpp,$(OUT)/%.o,$(CPP_SOURCES)) \
	  $(patsubst $(SERVER_OUT_DIR)/wire/%.cc,$(OUT)/wire/%.o,$(PROTO_SOURCES)) \
	$(OUT)/build.o
PROTO_H_FILES=$(patsubst $(SERVER_OUT_DIR)/wire/%.cc,$(SERVER_OUT_DIR)/wire/%.h,$(PROTO_SOURCES))

#sp: sound.cpp
#	g++ $(CFLAGS) sound.cpp -o sp $(LFLAGS) es

#mss: merge-sort-stack.cpp	
#	g++ merge-sort-stack.cpp -o mss

hc: $(OBJ_FILES)
	g++ $(OBJ_FILES) -o hc $(LFLAGS)

orbit: orbits.cpp
	g++ $(CFLAGS) orbits.cpp $(SERVER_OUT_DIR)/wire/terrain.pb.cc -o orbit $(LFLAGS)


$(OUT)/%.o: %.cpp $(PROTO_H_FILES)
	@mkdir -p $(OUT)
	g++ $(CFLAGS) -MD -c $< -o $@

$(OUT)/wire/%.o: $(SERVER_OUT_DIR)/wire/%.cc
	@mkdir -p $(OUT)/wire
	g++ $(CFLAGS) -MD -c $< -o $@

$(OUT)/build.c: .version ../../.git/index
	@mkdir -p $(OUT)
	./make-build-info > $(OUT)/build.c

$(OUT)/build.o: $(OUT)/build.c
	g++ -c $(OUT)/build.c -o $(OUT)/build.o

clean::
	rm -rf $(OUT)

-include $(OUT)/*.d
-include $(OUT)/wire/*.d
