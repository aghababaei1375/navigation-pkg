#include <navigation_pkg/AStar.h>
#include <navigation_pkg/Pose.h>

namespace navigation_pkg
{
    AStar::AStar(Vector2 _gridWorldSize, double _nodeRad, geometry_msgs::Point _worldBottomLeft, std::vector<std::vector<int>> data)
    :grid(_gridWorldSize, _nodeRad, _worldBottomLeft, data)
    {
        ros::NodeHandle nh;
        sub = nh.subscribe("/odom", 1, &AStar::OdomCallback, this);
        srv = nh.advertiseService("/global_planner_service", &AStar::GlobalPlanCallback, this);
        client = nh.serviceClient<navigation_pkg::Pose>("/DWA_LocalPlanner_Service");
    }

    void AStar::OdomCallback(nav_msgs::Odometry msg){
        currentPos.Set(
            msg.pose.pose.position.x,
            msg.pose.pose.position.y,
            msg.pose.pose.position.z
        );
    }

    bool AStar::GlobalPlanCallback(navigation_pkg::Target::Request& req, navigation_pkg::Target::Response& resp){
        ros::spinOnce();

        bool success = AStar::FindPath(currentPos, req.targetPos);
        resp.success = success;
        resp.path = grid.path;
        return success;
    }

    bool AStar::FindPath(Vector3 startPos, Vector3 targetPos){
        _time = ros::Time::now();
        bool success = false;

        Node* startNode = grid.NodeFromWorldPoint(startPos);
        Node* targetnode = grid.NodeFromWorldPoint(targetPos);

        ROS_INFO("StartNode  => %s", startNode->Print().c_str());
        ROS_INFO("TargetNode => %s", targetnode->Print().c_str());

        std::vector<Node*> openSet;
        std::vector<Node*> closedSet;

        openSet.push_back(startNode);
        int i = 0;

        // ROS_INFO("OpenSet Size(%d)", (int)openSet.size());
        while (openSet.size() > 0)
        {
            // ROS_INFO("Entered While.");
            Node* node = openSet[0];
            // ROS_INFO("openSet[0]\t=> %s", openSet[0]->Print().c_str());

            /**************************************/
            /* Finding the node with minimum cost */
            /**************************************/
            for (std::vector<Node*>::iterator it = openSet.begin(); it != openSet.end(); it++)
            {
                if ((*it)->fCost() < node->fCost() || (*it)->fCost() == node->fCost())
                {
                    if ((*it)->hCost < node->hCost)
                    {
                        node = *it;
                    }
                }
            }
            // ROS_INFO("node\t\t=> %s", node->Print().c_str());
            
            /*****************************************************************************/
            /* Removing the node (with minimum cost) from openSet and adding to closedSet*/
            /*****************************************************************************/
            for (std::vector<Node*>::iterator it = openSet.begin(); it < openSet.end(); it++)
            {
                if (*it == node)
                {
                    openSet.erase(it);
                    break;
                }
            }
            // ROS_INFO("OpenSet Size(%d)", (int)openSet.size());
            closedSet.push_back(node);
            // ROS_INFO("ClosedSet Size(%d)", (int)closedSet.size());
            
            /***********************************/
            /* Checking if we reach the target */
            /***********************************/
            if (node == targetnode)
            {
                ROS_INFO("Reached the target. Retracing the path.");
                success = true;
                AStar::RetracePath(startNode, targetnode);
                return success;
            }

            // ROS_INFO("Checking neighbours.");
            /************************************************************/
            /* Get the neighbours of the node and calculate their costs */
            /************************************************************/
            std::vector<Node*> neighbours = grid.GetNeighbours(node);
            // ROS_INFO("neighbours size(%d)", (int)neighbours.size());
            for (std::vector<Node*>::iterator it = neighbours.begin(); it < neighbours.end(); it++)
            {
                // ROS_INFO("Checking each neighbour.");
                if (!(*it)->walkable || AStar::Contain(&closedSet, *it)) continue;
                
                // ROS_INFO("Checking each prepared neighbour.");
                double newCostToNeighbour = node->gCost + AStar::GetDistance(node, *it);
                if (newCostToNeighbour < (*it)->gCost || !AStar::Contain(&openSet, *it))
                {
                    (*it)->gCost = newCostToNeighbour;
                    (*it)->hCost = AStar::GetDistance((*it), targetnode);
                    (*it)->parentX = node->gridX;
                    (*it)->parentY = node->gridY;
                    if (!AStar::Contain(&openSet, *it))
                    {
                        openSet.push_back(*it);
                    }
                    // ROS_INFO("Neighbour: %s", (*it)->Print().c_str());
                }
                
            }
            i++;
        }
        ROS_INFO("Finished FindPath, %s", success ? "Successfully" : "Failed");
        return success;
    }

    void AStar::RetracePath(Node* startNode, Node* endNode){
        ros::Duration t = ros::Time::now() - _time;
        ROS_INFO("Find Path completed in %f seconds.", t.toSec());
        _time = ros::Time::now();
        ROS_INFO("Entered Retraced Path");
        std::vector<Vector3> path;
        Node* currentNode = endNode;

        while (currentNode != startNode)
        {
            path.push_back(currentNode->worldPosition);
            currentNode = grid.NodeFromIndex(currentNode->parentX, currentNode->parentY);
        }

        std::reverse(path.begin(), path.end());
        grid.path = path;
        ROS_INFO("Path generated. Sending to Plan Follower...");
        
        //Send the path to PlanFollower

        t = ros::Time::now() - _time;
        ROS_INFO("Path reversed in %.15f seconds.", t.toSec());
        _time = ros::Time::now();

        ROS_INFO("Preliminary Path => Nodes: %d", (int)path.size());

        std::vector<geometry_msgs::Pose> pose;
        geometry_msgs::Pose old2, old1, curr;
        old2.position.x = path[0].x; old2.position.y = path[0].y; old2.position.z = path[0].z;
        pose.push_back(old2);
        old1.position.x = path[1].x; old1.position.y = path[1].y; old1.position.z = path[1].z;
        pose.push_back(old1);

        for (std::vector<navigation_pkg::Vector3>::iterator it = path.begin()+2; it != path.end(); it++)
        {
            curr.position.x = (*it).x; curr.position.y = (*it).y; curr.position.z = (*it).z;
            if (atan2(old1.position.y - old2.position.y, old1.position.x - old2.position.x) == atan2(curr.position.y - old1.position.y, curr.position.x - old1.position.x))
            {
                pose.pop_back();
            }
            pose.push_back(curr);
            old2 = old1;
            old1 = curr;
        }

        t = ros::Time::now() - _time;
        ROS_INFO("First Reduction of path completed in %.15f seconds.", t.toSec());
        _time = ros::Time::now();

        ROS_INFO("Path after firest reduction => Nodes: %d", (int)pose.size());

        std::vector<geometry_msgs::Pose> simplified_pose;
        old2.position.x = pose[0].position.x; old2.position.y = pose[0].position.y; old2.position.z = pose[0].position.z;
        simplified_pose.push_back(old2);
        old1.position.x = pose[1].position.x; old2.position.y = pose[1].position.y; old2.position.z = pose[1].position.z;
        simplified_pose.push_back(old1);

        for (std::vector<geometry_msgs::Pose>::iterator it = pose.begin()+2; it != pose.end(); it++)
        {
            curr = (*it);
            double theta = atan2(curr.position.y - old2.position.y, curr.position.x - old2.position.x);
            double l_max = sqrt(pow(curr.position.x - old2.position.x, 2) + pow(curr.position.y - old2.position.y, 2));
            double inc = 2.0e-2;
            bool collision = false;
            for (double l = 0; l < l_max; l += inc)
            {
                geometry_msgs::Pose P;
                P.position.x = old2.position.x + l * cos(theta);
                P.position.y = old2.position.y + l * sin(theta);

                Node* n = grid.NodeFromWorldPoint(*new Vector3(P.position.x, P.position.y, P.position.z));
                if (!n->walkable)
                {
                    collision = true;
                    break;
                }
            }
            if (!collision)
            {
                simplified_pose.pop_back();
            }
            simplified_pose.push_back(curr);
            old2 = old1;
            old1 = curr;
        }

        t = ros::Time::now() - _time;
        ROS_INFO("Second Reduction of path completed in %.15f seconds.", t.toSec());
        _time = ros::Time::now();

        ROS_INFO("Path after second reduction => Nodes: %d", (int)simplified_pose.size());
        
        navigation_pkg::Pose msg;
        msg.request.pose = simplified_pose;
        client.call(msg);
        

        grid.SavePathToFile();
       
    }

    double AStar::GetDistance(Node* nodeA, Node* nodeB){
        double dist = pow((nodeA->worldPosition.x - nodeB->worldPosition.x), 2) + pow((nodeA->worldPosition.y - nodeB->worldPosition.y), 2) + pow((nodeA->worldPosition.z - nodeB->worldPosition.z), 2); 
        return sqrt(dist);
    }

    bool AStar::Contain(std::vector<Node*>* vect, Node* node){
        for (std::vector<Node*>::iterator it = vect->begin(); it < vect->end(); it++)
        {
            if (node == (*it))
            {
                return true;
            }
        }
        return false;
    }

}; // namespace navigation_pkg
