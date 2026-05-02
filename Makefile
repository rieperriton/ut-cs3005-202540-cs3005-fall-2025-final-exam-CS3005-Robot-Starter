# Compiler
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic -fPIC
# Targets
all: test_robot SteepleSpecialist.so

RobotBase.o: RobotBase.cpp RobotBase.h
	$(CXX) $(CXXFLAGS) -c RobotBase.cpp

test_robot: test_robot.cpp RobotBase.o
	$(CXX) $(CXXFLAGS) test_robot.cpp RobotBase.o -ldl -o test_robot

SteepleSpecialist.so: SteepleSpecialist.cpp RobotBase.o
	$(CXX) $(CXXFLAGS) -fPIC -shared -o SteepleSpecialist.so SteepleSpecialist.cpp RobotBase.o

clean:
	rm -f *.o test_robot *.so
