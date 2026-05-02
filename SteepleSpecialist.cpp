#include "RobotBase.h"
#include <vector>
#include <algorithm>
#include <cstdlib> // std::abs
#include <set>
#include <limits>

class SteepleSpecialist : public RobotBase
{
private:
    bool target_found = false;
    int target_row = -1;
    int target_col = -1;

    bool fixed_radar = false; // Tracks whether radar is locked on a target
    int radar_direction = 1;
    std::set<std::pair<int,int>> obstacles_memory;
    //enemies spotted in one iteration of sweep. is cleared every turn
    struct Target { int row; int col; };
    std::vector<Target> m_targets;

    ///obstacles is used to identify walls.
    std::vector<RadarObj> m_obstacles;


    //we are trying to get to a corner for max protection. bottom left

    bool m_in_position = false;

    // Returns true if (row, col) is already tracked as a permanent obstacle.
    bool is_obstacle(int row, int col) const
    {
        return std::any_of(m_obstacles.begin(), m_obstacles.end(),
            [&](const RadarObj& o){ return o.m_row == row && o.m_col == col; });
    }


    //determines if someone is close enough to throw a grenade at them
    static int worth_targeting(int r1, int c1, int r2, int c2)
    { 
        return std::abs(r1 - r2) + std::abs(c1 - c2);
    }
    // Update the memory of obstacles
    void update_obstacle_memory(const std::vector<RadarObj>& radar_results) 
    {
        for (const auto& obj : radar_results) 
        {
            if (obj.m_type == 'M' || obj.m_type == 'P' || obj.m_type == 'F') 
            {
                obstacles_memory.insert({obj.m_row, obj.m_col});
            }
        }
    }

    static int calculate_distance(int r1, int c1, int r2, int c2)
    {
        return std::abs(r1 - r2) + std::abs(c1 - c2);
    }

    // Check if a cell is passable
    bool is_passable(int row, int col) const 
    {
        return obstacles_memory.find({row, col}) == obstacles_memory.end();
    }

    void find_closest_enemy(const std::vector<RadarObj>& radar_results, int current_row, int current_col) 
    {
        target_found = false;
        int max_range = 5;

        int closest_distance = std::numeric_limits<int>::max();


        m_targets.clear();

        for (const auto& obj : radar_results)
        {
            if (obj.m_type == 'R')
            {
                m_targets.push_back({obj.m_row, obj.m_col});  // ← ADD THIS

                int distance = calculate_distance(current_row, current_col, obj.m_row, obj.m_col);

                if (distance <= max_range && distance < closest_distance)
                {
                    closest_distance = distance;
                    target_row = obj.m_row;
                    target_col = obj.m_col;
                    target_found = true;
                    fixed_radar = true;
                }
            }
        }
    }
public:
    // move=2, armor=5, weapon=grenade
    SteepleSpecialist() : RobotBase(2, 5, grenade)
    {
        m_name = "Steeple_Specialist";
    }

    //iterates through all compass directions to see if target is there
    virtual void get_radar_direction(int& radar_direction_out) override 
    {
        if (fixed_radar && target_found) 
        {
            // Keep scanning the same direction if a target is found
            radar_direction_out = radar_direction;
        } 
        else 
        {
            // Cycle through radar directions (1-8) if no target
            radar_direction_out = radar_direction;
            radar_direction = (radar_direction % 8) + 1; // Increment and wrap around
        }
    }


    virtual void process_radar_results(const std::vector<RadarObj>& radar_results) override 
    {
        target_found = false; 
        int current_row, current_col;
        get_current_location(current_row, current_col);

        update_obstacle_memory(radar_results);

        find_closest_enemy(radar_results, current_row, current_col);

        if (!target_found) 
        {
            fixed_radar = false;
        }
    }

    

   
    virtual bool get_shot_location(int& shot_row, int& shot_col) override
    {
        if (m_targets.empty() || get_grenades() == 0)
            return false;

        int cur_row, cur_col;
        get_current_location(cur_row, cur_col);

        //locating best target 
        auto best = std::min_element(m_targets.begin(), m_targets.end(),
            [&](const Target& a, const Target& b)
            {
                return worth_targeting(cur_row, cur_col, a.row, a.col)
                     < worth_targeting(cur_row, cur_col, b.row, b.col);
            });

        shot_row = best->row;
        shot_col = best->col;
        return true;
    }


    //movement plan: get to bottom left, then start shooting everyone.
    //left/down repeatedly
    

    virtual void get_move_direction(int& move_direction, int& move_distance) override
    {
        int cur_row, cur_col;
        get_current_location(cur_row, cur_col);

        int speed       = get_move_speed();       //max of 2?
        int corner_row  = m_board_row_max - 1;    
        int corner_col  = 0;                      

        //move left until we reach column 0 
        if (cur_col > corner_col)
        {
            move_direction = 7; 
            move_distance  = std::min(speed, cur_col - corner_col);
            return;
        }

        if (cur_row < corner_row)
        {
            move_direction = 5; // South
            move_distance  = std::min(speed, corner_row - cur_row);
            return;
        }

        //no movement after final pos
        m_in_position  = true;
        move_direction = 0;   
        move_distance  = 0;
    }

    };

extern "C" RobotBase* create_robot()
{
    return new SteepleSpecialist();
}

extern "C" const char* robot_summary(); {
    return "This robot has a cornered puppy approach"

}

