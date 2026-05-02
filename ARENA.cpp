#include "RobotBase.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

static constexpr int kBoardRows = 15;
static constexpr int kBoardCols = 16;
static constexpr int kMaxRounds = 100;

struct ArenaRobot
{
    std::string name;
    std::string source_file;
    std::string shared_lib;
    void* handle = nullptr;
    RobotBase* robot = nullptr;
    bool alive = true;
    int row = 0;
    int col = 0;
    char marker = '?';
};

class Arena
{
public:
    Arena() : m_board(kBoardRows, std::string(kBoardCols, '.'))
    {
        build_static_map();
    }

    bool discover_and_compile_robots()
    {
        for (auto& entry : fs::directory_iterator(fs::current_path()))
        {
            if (!entry.is_regular_file())
                continue;

            std::string name = entry.path().filename().string();
            if (name.rfind("Robot_", 0) == 0 && entry.path().extension() == ".cpp")
            {
                fs::path source = entry.path();
                fs::path lib = source.parent_path() / ("lib" + source.stem().string() + ".so");
                if (!compile_robot(source, lib))
                    return false;

                ArenaRobot robot;
                robot.name = source.stem().string();
                robot.source_file = source.string();
                robot.shared_lib = lib.string();
                robot.marker = static_cast<char>('A' + static_cast<int>(m_robots.size()));
                m_robots.push_back(robot);
            }
        }

        if (m_robots.empty())
        {
            std::cerr << "No Robot_*.cpp files were found in the current directory.\n";
            return false;
        }

        return true;
    }

    bool load_robots()
    {
        for (auto& robot_entry : m_robots)
        {
            if (!load_library(robot_entry))
                return false;

            robot_entry.robot->set_boundaries(kBoardRows, kBoardCols);
        }
        return true;
    }

    bool place_robots()
    {
        std::vector<std::pair<int, int>> starting_points = {
            {0, 0},
            {0, kBoardCols - 1},
            {kBoardRows - 1, 0},
            {kBoardRows - 1, kBoardCols - 1},
            {0, kBoardCols / 2},
            {kBoardRows - 1, kBoardCols / 2}
        };

        int index = 0;
        for (auto& robot_entry : m_robots)
        {
            bool placed = false;
            while (index < static_cast<int>(starting_points.size()))
            {
                auto [r, c] = starting_points[index++];
                if (cell_is_free(r, c))
                {
                    robot_entry.row = r;
                    robot_entry.col = c;
                    robot_entry.robot->move_to(r, c);
                    placed = true;
                    break;
                }
            }

            if (!placed)
            {
                std::cerr << "Not enough starting positions for all robots.\n";
                return false;
            }
        }

        return true;
    }

    void run()
    {
        int round = 1;
        while (round <= kMaxRounds && alive_robot_count() > 1)
        {
            m_round_log.str("");
            m_round_log.clear();
            execute_round(round);

            std::system("clear");
            std::cout << "=== Round " << round << " ===\n";
            print_board();
            print_stats();
            std::cout << "\n-- Round Log --\n" << m_round_log.str();
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            ++round;
        }

        std::system("clear");
        print_board();
        print_stats();
        announce_winner();
    }

    ~Arena()
    {
        for (auto& robot_entry : m_robots)
        {
            if (robot_entry.robot)
                delete robot_entry.robot;
            if (robot_entry.handle)
                dlclose(robot_entry.handle);
        }
    }

private:
    std::vector<std::string> m_board;
    std::vector<ArenaRobot> m_robots;
    std::ostringstream m_round_log;

    void build_static_map()
    {
        std::vector<std::string> preset = {
            "...............",
            "..M...P....F....",
            ".P..M...M...P..",
            ".....F......M...",
            "..M.......P.....",
            "....P...M....P..",
            ".F....M.....F...",
            "...M...P...M....",
            "....P.....M.....",
            ".M.....F...P....",
            "...P...M..F.....",
            "...M.........M..",
            "....F....P......",
            "..P.....M....F..",
            "..............."
        };

        for (int r = 0; r < kBoardRows; ++r)
        {
            for (int c = 0; c < kBoardCols; ++c)
            {
                m_board[r][c] = preset[r][c];
            }
        }
    }

    bool compile_robot(const fs::path& cpp_path, const fs::path& lib_path)
    {
        std::ostringstream cmd;
        cmd << "g++ -std=c++20 -Wall -Wextra -pedantic -fPIC -shared -o "
            << lib_path.string() << " " << cpp_path.string() << " RobotBase.o -I. ";

        std::cout << "Compiling " << cpp_path.filename().string() << " into " << lib_path.filename().string() << "...\n";
        int result = std::system(cmd.str().c_str());
        if (result != 0)
        {
            std::cerr << "Failed to compile " << cpp_path.filename().string() << "\n";
            return false;
        }
        return true;
    }

    bool load_library(ArenaRobot& robot_entry)
    {
        robot_entry.handle = dlopen(robot_entry.shared_lib.c_str(), RTLD_LAZY);
        if (!robot_entry.handle)
        {
            std::cerr << "Failed to load " << robot_entry.shared_lib << ": " << dlerror() << "\n";
            return false;
        }

        RobotFactory create_robot = reinterpret_cast<RobotFactory>(dlsym(robot_entry.handle, "create_robot"));
        if (!create_robot)
        {
            std::cerr << "create_robot symbol not found in " << robot_entry.shared_lib << ": " << dlerror() << "\n";
            return false;
        }

        robot_entry.robot = create_robot();
        if (!robot_entry.robot)
        {
            std::cerr << "Failed to instantiate robot from " << robot_entry.shared_lib << "\n";
            return false;
        }

        return true;
    }

    int alive_robot_count() const
    {
        return std::count_if(m_robots.begin(), m_robots.end(), [](auto const& robot) { return robot.alive; });
    }

    bool cell_is_free(int row, int col) const
    {
        if (!in_bounds(row, col))
            return false;

        if (m_board[row][col] == 'M')
            return false;

        for (auto const& robot_entry : m_robots)
        {
            if (robot_entry.alive && robot_entry.row == row && robot_entry.col == col)
                return false;
        }

        return true;
    }

    bool in_bounds(int row, int col) const
    {
        return row >= 0 && row < kBoardRows && col >= 0 && col < kBoardCols;
    }

    void print_arena_header() const
    {
        std::cout << "RobotWarz Arena " << kBoardRows << "x" << kBoardCols << "\n";
        std::cout << "Discovered " << m_robots.size() << " robot" << (m_robots.size() == 1 ? "" : "s") << ".\n";
    }

    void print_board() const
    {
        std::cout << "\nBoard:\n  ";
        for (int col = 0; col < kBoardCols; ++col)
            std::cout << (col % 10);
        std::cout << "\n";

        for (int r = 0; r < kBoardRows; ++r)
        {
            std::cout << (r % 10) << " ";
            for (int c = 0; c < kBoardCols; ++c)
            {
                char tile = m_board[r][c];
                const ArenaRobot* robot_at = robot_at_position(r, c);
                if (robot_at)
                {
                    if (robot_at->alive)
                        std::cout << robot_at->marker;
                    else
                        std::cout << 'X';
                }
                else
                {
                    std::cout << tile;
                }
            }
            std::cout << '\n';
        }
    }

    const ArenaRobot* robot_at_position(int row, int col) const
    {
        for (auto const& robot_entry : m_robots)
        {
            if (robot_entry.row == row && robot_entry.col == col)
                return &robot_entry;
        }
        return nullptr;
    }

    ArenaRobot* robot_at_position(int row, int col)
    {
        for (auto& robot_entry : m_robots)
        {
            if (robot_entry.row == row && robot_entry.col == col)
                return &robot_entry;
        }
        return nullptr;
    }

    void print_stats() const
    {
        std::cout << "\nRobot status:\n";
        for (auto const& robot_entry : m_robots)
        {
            std::cout << "[" << robot_entry.marker << "] " << robot_entry.name << " "
                      << (robot_entry.alive ? "ALIVE" : "DEAD")
                      << " at (" << robot_entry.row << "," << robot_entry.col << ")";
            if (robot_entry.alive)
            {
                std::cout << " HP=" << robot_entry.robot->get_health()
                          << " A=" << robot_entry.robot->get_armor()
                          << " M=" << robot_entry.robot->get_move_speed();
                if (robot_entry.robot->get_weapon() == grenade)
                    std::cout << " G=" << robot_entry.robot->get_grenades();
            }
            std::cout << "\n";
        }
    }

    void execute_round(int round_number)
    {
        (void)round_number;
        for (auto& robot_entry : m_robots)
        {
            if (!robot_entry.alive)
                continue;

            int current_row = robot_entry.row;
            int current_col = robot_entry.col;
            m_round_log << "\n[" << robot_entry.marker << "] " << robot_entry.name
                        << " begins turn at (" << current_row << "," << current_col << ")\n";

            int radar_direction = 0;
            robot_entry.robot->get_radar_direction(radar_direction);
            if (radar_direction < 1 || radar_direction > 8)
            {
                m_round_log << "  Invalid radar direction " << radar_direction << ", using 1 instead.\n";
                radar_direction = 1;
            }
            m_round_log << "  Radar direction: " << radar_direction << "\n";

            std::vector<RadarObj> radar_results = scan_radar(robot_entry, radar_direction);
            if (radar_results.empty())
                m_round_log << "  Radar scanned nothing.\n";
            else
            {
                m_round_log << "  Radar detected:";
                for (auto const& obj : radar_results)
                    m_round_log << " " << obj.m_type << "(" << obj.m_row << "," << obj.m_col << ")";
                m_round_log << "\n";
            }

            robot_entry.robot->process_radar_results(radar_results);

            int shot_row = 0;
            int shot_col = 0;
            if (robot_entry.robot->get_shot_location(shot_row, shot_col))
            {
                m_round_log << "  Shooting at (" << shot_row << "," << shot_col << ")\n";
                resolve_shot(robot_entry, shot_row, shot_col);
            }
            else
            {
                m_round_log << "  No shot fired.\n";
            }

            int move_direction = 0;
            int move_distance = 0;
            robot_entry.robot->get_move_direction(move_direction, move_distance);
            if (move_direction < 1 || move_direction > 8 || move_distance <= 0)
            {
                m_round_log << "  No movement chosen.\n";
            }
            else
            {
                m_round_log << "  Wants to move direction " << move_direction
                            << " distance " << move_distance << "\n";
                resolve_movement(robot_entry, move_direction, move_distance);
            }

            if (robot_entry.alive)
            {
                int updated_row, updated_col;
                robot_entry.robot->get_current_location(updated_row, updated_col);
                robot_entry.row = updated_row;
                robot_entry.col = updated_col;
            }
        }
    }

    std::vector<RadarObj> scan_radar(const ArenaRobot& robot_entry, int direction) const
    {
        std::vector<RadarObj> results;
        int dr = directions[direction].first;
        int dc = directions[direction].second;

        int r = robot_entry.row + dr;
        int c = robot_entry.col + dc;
        while (in_bounds(r, c))
        {
            const ArenaRobot* target = robot_at_position(r, c);
            if (target && target != &robot_entry)
            {
                results.emplace_back(target->alive ? 'R' : 'X', r, c);
            }
            char tile = m_board[r][c];
            if (tile == 'M' || tile == 'P' || tile == 'F')
                results.emplace_back(tile, r, c);

            r += dr;
            c += dc;
        }

        return results;
    }

    int weapon_damage(WeaponType weapon) const
    {
        switch (weapon)
        {
            case flamethrower: return 25;
            case railgun:      return 40;
            case grenade:      return 50;
            case hammer:       return 20;
            default:           return 10;
        }
    }

    void resolve_shot(ArenaRobot& shooter, int shot_row, int shot_col)
    {
        if (!in_bounds(shot_row, shot_col))
        {
            m_round_log << "  Shot location is out of bounds and misses.\n";
            return;
        }

        ArenaRobot* target = robot_at_position(shot_row, shot_col);
        if (!target || !target->alive)
        {
            m_round_log << "  No living robot at target location; shot misses.\n";
            return;
        }

        if (target == &shooter)
        {
            m_round_log << "  Robot attempted to shoot itself; shot ignored.\n";
            return;
        }

        int damage = weapon_damage(shooter.robot->get_weapon());
        int armor = target->robot->get_armor();
        int armor_absorb = std::min(armor, damage / 2);
        target->robot->reduce_armor(armor_absorb);
        int health_damage = std::max(0, damage - armor_absorb);
        target->robot->take_damage(health_damage);

        m_round_log << "  Hit " << target->name << " for " << health_damage
                    << " damage, armor absorbed " << armor_absorb << ".\n";

        if (shooter.robot->get_weapon() == grenade)
        {
            shooter.robot->decrement_grenades();
            m_round_log << "  " << shooter.name << " now has " << shooter.robot->get_grenades() << " grenades.\n";
        }

        if (target->robot->get_health() == 0)
        {
            target->alive = false;
            m_round_log << "  " << target->name << " has been destroyed!\n";
        }
    }

    void resolve_movement(ArenaRobot& robot_entry, int direction, int distance)
    {
        if (!robot_entry.alive)
            return;

        if (robot_entry.robot->get_move_speed() == 0)
        {
            m_round_log << "  Movement disabled by pit or zero move speed.\n";
            return;
        }

        int dr = directions[direction].first;
        int dc = directions[direction].second;

        int current_row = robot_entry.row;
        int current_col = robot_entry.col;
        int target_row = current_row;
        int target_col = current_col;

        for (int step = 0; step < distance; ++step)
        {
            int next_row = target_row + dr;
            int next_col = target_col + dc;
            if (!in_bounds(next_row, next_col))
            {
                m_round_log << "  Movement stops at boundary.\n";
                break;
            }
            char tile = m_board[next_row][next_col];
            if (tile == 'M')
            {
                m_round_log << "  Movement blocked by mound at (" << next_row << "," << next_col << ").\n";
                break;
            }
            ArenaRobot* occupant = robot_at_position(next_row, next_col);
            if (occupant && occupant->alive)
            {
                m_round_log << "  Movement blocked by " << occupant->name << " at (" << next_row << "," << next_col << ").\n";
                break;
            }

            target_row = next_row;
            target_col = next_col;
        }

        if (target_row == robot_entry.row && target_col == robot_entry.col)
        {
            m_round_log << "  Robot remains at its current location.\n";
            return;
        }

        robot_entry.robot->move_to(target_row, target_col);
        robot_entry.row = target_row;
        robot_entry.col = target_col;
        m_round_log << "  Moved to (" << target_row << "," << target_col << ").\n";

        char tile = m_board[target_row][target_col];
        if (tile == 'P')
        {
            robot_entry.robot->disable_movement();
            m_round_log << "  Entered a pit; movement disabled for the rest of the game.\n";
        }
        else if (tile == 'F')
        {
            int damage = weapon_damage(flamethrower);
            int armor = robot_entry.robot->get_armor();
            int armor_absorb = std::min(armor, damage / 2);
            robot_entry.robot->reduce_armor(armor_absorb);
            int health_damage = std::max(0, damage - armor_absorb);
            robot_entry.robot->take_damage(health_damage);
            m_round_log << "  Stepped on flamethrower; took " << health_damage
                        << " damage and " << armor_absorb << " armor absorption.\n";
            if (robot_entry.robot->get_health() == 0)
            {
                robot_entry.alive = false;
                m_round_log << "  " << robot_entry.name << " has been destroyed by the obstacle!\n";
            }
        }
    }

    void announce_winner() const
    {
        int alive_count = alive_robot_count();
        if (alive_count == 1)
        {
            for (auto const& robot_entry : m_robots)
            {
                if (robot_entry.alive)
                {
                    std::cout << "\nWinner: " << robot_entry.name << " (" << robot_entry.marker << ")!\n";
                    return;
                }
            }
        }
        else if (alive_count == 0)
        {
            std::cout << "\nAll robots have been destroyed. No winner.\n";
        }
        else
        {
            std::cout << "\nNo winner after " << kMaxRounds << " rounds. Surviving robots remain.\n";
        }
    }
};

int main()
{
    Arena arena;
    if (!arena.discover_and_compile_robots())
        return 1;
    if (!arena.load_robots())
        return 1;
    if (!arena.place_robots())
        return 1;

    arena.run();
    return 0;
}