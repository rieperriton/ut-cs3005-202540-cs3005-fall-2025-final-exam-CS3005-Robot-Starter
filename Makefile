# Compiler
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic -fPIC
# Targets
all: test_robot RobotWarz

RobotBase.o: RobotBase.cpp RobotBase.h
	$(CXX) $(CXXFLAGS) -c RobotBase.cpp

test_robot: test_robot.cpp RobotBase.o
	$(CXX) $(CXXFLAGS) test_robot.cpp RobotBase.o -ldl -o test_robot

ARENA: ARENA.cpp RobotBase.o
	$(CXX) $(CXXFLAGS) ARENA.cpp RobotBase.o -ldl -o ARENA

clean:
	rm -f *.o test_robot ARENA *.so
