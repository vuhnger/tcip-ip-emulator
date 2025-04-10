#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "maze.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "maze.h"

// Queue structure for BFS
typedef struct {
    int x;
    int y;
    int prev;  // Index of previous cell in queue
} QueueNode;

// Solve maze using BFS (Breadth-First Search)
static int solveMazeBFS(struct Maze* maze) {
    // Allocate visited array to avoid revisiting cells
    char* visited = calloc(maze->size, sizeof(char));
    if (!visited) {
        fprintf(stderr, "solveMazeBFS: Memory allocation failed for visited array\n");
        return 0;
    }
    
    // Allocate queue for BFS
    QueueNode* queue = malloc(maze->size * sizeof(QueueNode));
    if (!queue) {
        fprintf(stderr, "solveMazeBFS: Memory allocation failed for queue\n");
        free(visited);
        return 0;
    }
    
    // Initialize queue with starting position
    int front = 0;
    int rear = 1;
    queue[0].x = maze->startX;
    queue[0].y = maze->startY;
    queue[0].prev = -1;  // No previous node for start
    
    // Mark starting position as visited
    visited[maze->startY * maze->edgeLen + maze->startX] = 1;
    
    // Direction arrays for moving right, down, left, up
    int dx[] = {1, 0, -1, 0};
    int dy[] = {0, 1, 0, -1};
    int dir_bits[] = {right, down, left, up};
    
    // Flag to indicate if we found the target
    int found = 0;
    int target_idx = -1;
    
    // BFS loop
    while (front < rear && !found) {
        // Get current cell
        int curr_x = queue[front].x;
        int curr_y = queue[front].y;
        int curr_idx = curr_y * maze->edgeLen + curr_x;
        
        // Check all four directions
        for (int dir = 0; dir < 4; dir++) {
            // Check if passage exists in this direction
            if (maze->maze[curr_idx] & dir_bits[dir]) {
                int new_x = curr_x + dx[dir];
                int new_y = curr_y + dy[dir];
                
                // Check if the new position is valid
                if (new_x >= 0 && new_x < maze->edgeLen && 
                    new_y >= 0 && new_y < maze->edgeLen) {
                    
                    int new_idx = new_y * maze->edgeLen + new_x;
                    
                    // Check if not visited
                    if (!visited[new_idx]) {
                        // Add to queue
                        queue[rear].x = new_x;
                        queue[rear].y = new_y;
                        queue[rear].prev = front;
                        visited[new_idx] = 1;
                        
                        // Check if we found the target
                        if (new_x == maze->endX && new_y == maze->endY) {
                            found = 1;
                            target_idx = rear;
                            break;
                        }
                        
                        rear++;
                    }
                }
            }
        }
        
        front++;
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
            curr = queue[curr].prev;
        }
    }
    
    // Free allocated memory
    free(visited);
    free(queue);
    
    return found;
}

void mazeSolve(struct Maze* maze) {
    if (maze == NULL) {
        fprintf(stderr, "mazeSolve: Invalid maze pointer\n");
        return;
    }
    
    // Check maze parameters
    if (maze->edgeLen == 0 || maze->size != maze->edgeLen * maze->edgeLen) {
        fprintf(stderr, "mazeSolve: Invalid maze dimensions\n");
        return;
    }
    
    // Check start and end coordinates
    if (maze->startX >= maze->edgeLen || maze->startY >= maze->edgeLen ||
        maze->endX >= maze->edgeLen || maze->endY >= maze->edgeLen) {
        fprintf(stderr, "mazeSolve: Start or end position out of bounds\n");
        return;
    }
    
    fprintf(stderr, "Solving maze %dx%d from (%d,%d) to (%d,%d) using BFS\n", 
            maze->edgeLen, maze->edgeLen, 
            maze->startX, maze->startY, 
            maze->endX, maze->endY);
    
    // Create a backup of the maze in case we need to restore it
    char* mazeCopy = malloc(maze->size);
    if (mazeCopy == NULL) {
        fprintf(stderr, "mazeSolve: Failed to allocate memory for maze backup\n");
        return;
    }
    memcpy(mazeCopy, maze->maze, maze->size);
    
    // Solve the maze using breadth-first search
    int result = solveMazeBFS(maze);
    
    if (result) {
        fprintf(stderr, "mazeSolve: Solution found with BFS (shortest path)!\n");
    } else {
        fprintf(stderr, "mazeSolve: No solution exists for this maze\n");
        
        // Restore original maze without any marks
        memcpy(maze->maze, mazeCopy, maze->size);
    }
    
    // Free the maze backup
    free(mazeCopy);
}