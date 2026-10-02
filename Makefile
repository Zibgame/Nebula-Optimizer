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

stop-nebula:
	@powershell.exe -NoProfile -Command "\$$items=Get-CimInstance Win32_Process -Filter \"Name='$(NAME)'\" -ErrorAction SilentlyContinue; \$$items | Where-Object { \$$_.CommandLine -notmatch '--watchdog' } | ForEach-Object { Stop-Process -Id \$$_.ProcessId -Force -ErrorAction SilentlyContinue }; \$$limit=(Get-Date).AddSeconds(15); while ((Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension('$(NAME)')) -ErrorAction SilentlyContinue) -and (Get-Date) -lt \$$limit) { Start-Sleep -Milliseconds 100 }"

$(NAME): stop-nebula $(OBJ) $(RC_OBJ) Makefile
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
	@for i in 1 2 3 4 5; do rm -f obj/test_preferences.exe && break; sleep 1; done

test-navigation:
	$(CXX) $(CXXFLAGS) tests/test_navigation.cpp src/optimizer/optimizer.cpp src/optimizer/saved_tweaks.cpp \
		$(LDFLAGS) -o obj/test_navigation.exe
	obj/test_navigation.exe
	@for i in 1 2 3 4 5; do rm -f obj/test_navigation.exe && break; sleep 1; done

test-saved-scan:
	$(CXX) $(CXXFLAGS) tests/test_saved_scan.cpp src/optimizer/saved_tweaks.cpp \
		$(LDFLAGS) -o obj/test_saved_scan.exe
	obj/test_saved_scan.exe
	@for i in 1 2 3 4 5; do rm -f obj/test_saved_scan.exe && break; sleep 1; done

test-tui-view:
	$(CXX) $(CXXFLAGS) tests/test_tui_view.cpp $(LDFLAGS) -o obj/test_tui_view.exe
	obj/test_tui_view.exe
	@for i in 1 2 3 4 5; do rm -f obj/test_tui_view.exe && break; sleep 1; done

test-background-apps:
	$(CXX) $(CXXFLAGS) tests/test_background_apps.cpp src/ui/background_apps.cpp \
		$(LDFLAGS) -o obj/test_background_apps.exe
	obj/test_background_apps.exe
	@for i in 1 2 3 4 5; do rm -f obj/test_background_apps.exe && break; sleep 1; done

test-presentmon:
	$(CXX) $(CXXFLAGS) tests/test_presentmon_metrics.cpp $(LDFLAGS) -o obj/test_presentmon_metrics.exe
	obj/test_presentmon_metrics.exe
	@for i in 1 2 3 4 5; do rm -f obj/test_presentmon_metrics.exe && break; sleep 1; done

test-power-qos:
	$(CXX) $(CXXFLAGS) tests/test_power_qos.cpp $(LDFLAGS) -o obj/test_power_qos.exe
	obj/test_power_qos.exe
	@for i in 1 2 3 4 5; do rm -f obj/test_power_qos.exe && break; sleep 1; done

.PHONY: all stop-nebula clean fclean re test-preferences test-navigation test-saved-scan test-tui-view test-background-apps test-presentmon test-power-qos
