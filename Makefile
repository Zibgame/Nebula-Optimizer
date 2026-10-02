NAME = Nebula-Optimizer.exe
.DEFAULT_GOAL := all

SHELL := sh.exe

CXX = g++
WINDRES = windres
WINDRES_CPP := $(shell cygpath -m "$$(command -v gcc)")

CXXFLAGS = -Wall -Wextra -Werror -std=c++17 -MMD -MP \
	-Iincludes \
	-Iincludes/ui \
	-Iincludes/json \
	-Iincludes/optimizer

LDFLAGS = -static -static-libgcc -static-libstdc++ -lwinmm -lpdh -ladvapi32 -lshell32 -lpsapi -lpowrprof -lole32 -liphlpapi -lws2_32 -lcfgmgr32

ICON = src/image/icon/other.ico
MANIFEST = nebula_optimizer.manifest
RC_FILE = nebula_optimizer.rc
RC_OBJ = obj/nebula_optimizer_res.o

SRC_DIR = src
OBJ_DIR = obj

SRC = \
	$(wildcard $(SRC_DIR)/*.cpp) \
	$(wildcard $(SRC_DIR)/ui/*.cpp) \
	$(wildcard $(SRC_DIR)/json/*.cpp) \
	$(wildcard $(SRC_DIR)/optimizer/*.cpp)

OBJ = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC))
-include $(OBJ:.o=.d)

all: $(NAME)

$(NAME): $(OBJ) $(RC_OBJ) Makefile
	$(CXX) $(CXXFLAGS) $(OBJ) $(RC_OBJ) $(LDFLAGS) -o $(NAME)

$(RC_OBJ): $(RC_FILE) $(ICON) $(MANIFEST)
	@mkdir -p "$(@D)"
	$(WINDRES) --preprocessor="$(WINDRES_CPP) -E -xc -DRC_INVOKED" $(RC_FILE) -O coff -o $(RC_OBJ)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -rf "$(OBJ_DIR)"

fclean: clean
	@rm -f "$(NAME)"

re: fclean all

test-preferences:
	$(CXX) $(CXXFLAGS) tests/test_preferences.cpp src/optimizer/optimizer.cpp src/optimizer/saved_tweaks.cpp \
		$(LDFLAGS) -o obj/test_preferences.exe
	obj/test_preferences.exe
	@sleep 1
	@rm -f obj/test_preferences.exe

test-navigation:
	$(CXX) $(CXXFLAGS) tests/test_navigation.cpp src/optimizer/optimizer.cpp src/optimizer/saved_tweaks.cpp \
		$(LDFLAGS) -o obj/test_navigation.exe
	obj/test_navigation.exe
	@sleep 1
	@rm -f obj/test_navigation.exe

test-saved-scan:
	$(CXX) $(CXXFLAGS) tests/test_saved_scan.cpp src/optimizer/saved_tweaks.cpp \
		$(LDFLAGS) -o obj/test_saved_scan.exe
	obj/test_saved_scan.exe
	@sleep 1
	@rm -f obj/test_saved_scan.exe

test-tui-view:
	$(CXX) $(CXXFLAGS) tests/test_tui_view.cpp $(LDFLAGS) -o obj/test_tui_view.exe
	obj/test_tui_view.exe
	@sleep 1
	@rm -f obj/test_tui_view.exe

test-background-apps:
	$(CXX) $(CXXFLAGS) tests/test_background_apps.cpp src/ui/background_apps.cpp \
		$(LDFLAGS) -o obj/test_background_apps.exe
	obj/test_background_apps.exe
	@sleep 5
	@rm -f obj/test_background_apps.exe

test-presentmon:
	$(CXX) $(CXXFLAGS) tests/test_presentmon_metrics.cpp $(LDFLAGS) -o obj/test_presentmon_metrics.exe
	obj/test_presentmon_metrics.exe
	@sleep 1
	@rm -f obj/test_presentmon_metrics.exe

test-power-qos:
	$(CXX) $(CXXFLAGS) tests/test_power_qos.cpp $(LDFLAGS) -o obj/test_power_qos.exe
	obj/test_power_qos.exe
	@sleep 1
	@rm -f obj/test_power_qos.exe

.PHONY: all clean fclean re test-preferences test-navigation test-saved-scan test-tui-view test-background-apps test-presentmon test-power-qos
