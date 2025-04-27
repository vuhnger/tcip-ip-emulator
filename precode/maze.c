#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "maze.h"

// Queue structure for BFS
typedef struct {
    int x;
    int y;
    int prevIndex;  // Index of previous cell in queue
} Cell;



// Solve maze using BFS (Breadth-First Search)
static int solveMazeBFS(struct Maze* maze) {
    // Allocate visited array to avoid revisiting cells
    char* visited = calloc(maze->size, sizeof(char));
    if (!visited) {
        fprintf(stderr, "%s: memory allocation failed for visited\n", __FUNCTION__);
        return 0;
    }
    
    // Allocate queue for BFS
    Cell* queue = malloc(maze->size * sizeof(Cell));
    if (!queue) {
        fprintf(stderr, "%s: memory allocation failed for queue\n", __FUNCTION__);
        free(visited);
        return 0;
    }

    int headIndex = 0;
    int tailIndex = 1;
    queue[0].x = maze->startX;
    queue[0].y = maze->startY;
    queue[0].prevIndex = -1;
    
    // position = y * _edgeLen + x
    // mark start as visited
    visited[maze->startY * maze->edgeLen + maze->startX] = 1;

    typedef struct {
    int dx, dy, bit;
    } Direction;

    Direction directions[] = {
        {1, 0, right},   // move right
        {0, 1, down},    // move down
        {-1, 0, left},   // move left
        {0, -1, up}      // move up
    };
    
    // Flag to indicate if we found the target
    int found = 0;
    int target_idx = -1;
    
    // BFS loop
    while (headIndex < tailIndex && !found) {
        // Get current cell
        int curr_x = queue[headIndex].x;
        int curr_y = queue[headIndex].y;
        int curr_idx = curr_y * maze->edgeLen + curr_x;
        
        // Check all four directions
        for (int dir = 0; dir < 4; dir++) {
            // If no passage in this direction, skip
            if (!(maze->maze[curr_idx] & directions[dir].bit))
                continue;
            
            int new_x = curr_x + directions[dir].dx;
            int new_y = curr_y + directions[dir].dy;

            // If new position is out of bounds, skip
            if (new_x < 0 || new_x >= maze->edgeLen || 
                new_y < 0 || new_y >= maze->edgeLen)
                continue;

            int new_idx = new_y * maze->edgeLen + new_x;

            // If already visited, skip
            if (visited[new_idx])
                continue;

            // Add to queue
            queue[tailIndex].x = new_x;
            queue[tailIndex].y = new_y;
            queue[tailIndex].prevIndex = headIndex;
            visited[new_idx] = 1;

            // Check if target found
            if (new_x == maze->endX && new_y == maze->endY) {
                found = 1;
                target_idx = tailIndex;
                break;
            }

            tailIndex++;
        }
        
        headIndex++;
    }
    
    // If target found, trace back the path and mark it
    if (found) {
        // Start from the target and go back to the start
        int curr = target_idx;
        while (curr != -1) {
            int x = queue[curr].x;
            int y = queue[curr].y;
            // Mark the cell as part of the path
            maze->maze[y * maze->edgeLen + x] |= mark;
            curr = queue[curr].prevIndex;
        }
    }
    
    // Free allocated memory
    free(visited);
    free(queue);
    
    return found;
}

void mazeSolve(struct Maze* maze) {
    
    if (!maze) {
        fprintf(stderr, "%s: null maze pointer\n",__FUNCTION__);
        return;
    }
    
    int hasValidMazeDimensions = maze->edgeLen > 0 && maze->size == maze->edgeLen * maze->edgeLen;

    int hasValidStartEndCoordinates = 
        maze->startX < maze->edgeLen &&
        maze->startY < maze->edgeLen &&
        maze->endX < maze->edgeLen &&
        maze->endY < maze->edgeLen;

    if (!hasValidMazeDimensions) {
        fprintf(stderr, "%s: invalid maze dimensions\n", __FUNCTION__);
        return;
    }

    if (!hasValidStartEndCoordinates) {
        fprintf(stderr, "%s: invalid start or end position\n", __FUNCTION__);
        return;
    }
    
    int result = solveMazeBFS(maze);
    
    // remove temp marks
    for (uint32_t i = 0; i < maze->size; i++) {
        maze->maze[i] &= ~tmark;  
    }
    
    fprintf(stderr, "%s: solved maze! ;D\n",__FUNCTION__);
}