/**
 * swr - a software rasterizer
 * 
 * cube geometry definition.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2021
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#ifdef VERTEX_LIST

VERTEX_LIST(

/* front/back */
{-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1},
{-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
            
/* top/bottom */
{ 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1},
{-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}, {-1,  1, -1},
            
/* left/right */
{-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1},
{-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}

) /* VERTEX_LIST */

#endif /* VERTEX_LIST */

#ifdef COLOR_LIST

COLOR_LIST(

/* front/back */
{0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f},
{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},

/* top/bottom */
{1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f},

/* left/right */
{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f},
{0.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}

) /* COLOR_LIST */

#endif /* COLOR_LIST */

#ifdef NORMAL_LIST

NORMAL_LIST(

/* front/back */
{0,0, 1,0}, {0,0, 1,0}, {0,0, 1,0}, {0,0, 1,0},
{0,0,-1,0}, {0,0,-1,0}, {0,0,-1,0}, {0,0,-1,0},

/* top/bottom */
{ 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0},
{-1,0,0,0}, {-1,0,0,0}, {-1,0,0,0}, {-1,0,0,0},

/* left/right */
{0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0},
{0, 1,0,0}, {0, 1,0,0}, {0, 1,0,0}, {0, 1,0,0},

) /* NORMAL_LIST */

#endif /* NORMAL_LIST */

#ifdef TANGENT_LIST

TANGENT_LIST(

/* front/back */
{ 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0},
{ 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0},

/* top/bottom */
{0,0,-1,0}, {0,0,-1,0}, {0,0,-1,0}, {0,0,-1,0},
{0,0, 1,0}, {0,0, 1,0}, {0,0, 1,0}, {0,0, 1,0},

/* left/right */
{ 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0},
{ 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0}, { 1,0,0,0},

) /* TANGENT_LIST */

#endif /* TANGENT_LIST */

#ifdef BITANGENT_LIST

BITANGENT_LIST(

/* front/back */
{0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0},
{0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0},

/* top/bottom */
{0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0},
{0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0}, {0,-1,0,0},

/* left/right */
{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0},
{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0},

) /* BITANGENT_LIST */

#endif /* BITANGENT_LIST */

#ifdef UV_LIST

UV_LIST(

/* front/back */
{0,2,0,0}, {2,2,0,0}, {2,0,0,0}, {0,0,0,0},
{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0},

/* top/bottom */
{0,0,0,0}, {0,1,0,0}, {1,1,0,0}, {1,0,0,0},
{0,0,0,0}, {0,1,0,0}, {1,1,0,0}, {1,0,0,0},

/* left/right */
{0,0,0,0}, {1,0,0,0}, {1,1,0,0}, {0,1,0,0},
{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0},

) /* UV_LIST */

#endif /* UV_LIST */

#ifdef FACE_LIST

FACE_LIST(

/* front */
0, 1, 2,
2, 3, 0,

/* top */
8, 9, 10,
10, 11, 8,

/* back */
7, 6, 5,
5, 4, 7,

/* bottom */
12, 13, 14,
14, 15, 12,

/* left */
16, 17, 18,
18, 19, 16,

/* right */
20, 21, 22,
22, 23, 20

) /* FACE_LIST */

#endif /* FACE_LIST */
