#pragma once

void showBootScreen();  // blocking boot animation, called once in setup()
void updateDisplay();   // called each active loop tick
void updateTaskEval();  // task-eval state tick (home screen only)
