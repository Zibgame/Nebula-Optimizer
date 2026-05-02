NAME = nebula_optimizer.exe

CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++17 \
	-Iincludes \
	-Iincludes/ui \
	-Iincludes/json \
	-Iincludes/optimizer

SRC_DIR = src
OBJ_DIR = obj

SRC = \
	$(wildcard $(SRC_DIR)/*.cpp) \
	$(wildcard $(SRC_DIR)/ui/*.cpp) \
	$(wildcard $(SRC_DIR)/json/*.cpp) \
	$(wildcard $(SRC_DIR)/optimizer/*.cpp)

OBJ = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC))

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@if exist "$(OBJ_DIR)" rmdir /s /q "$(OBJ_DIR)"

fclean: clean
	@if exist "$(NAME)" del /f /q "$(NAME)"

re: fclean all

.PHONY: all clean fclean re