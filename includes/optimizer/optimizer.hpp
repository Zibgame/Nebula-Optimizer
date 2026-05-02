#pragma once

#include <string>
#include <iostream>
#include <windows.h>

class Optimizer {
    public:
        Optimizer();
        ~Optimizer();

        void optimize(const std::string& game_path);
        void restore();
    
    private:
        bool _is_optimized = false;
        bool _is_fortnite = false;
};