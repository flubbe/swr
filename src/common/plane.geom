/**
 * swr - a software rasterizer
 * 
 * plane geometry definition.
 * 
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#ifdef VERTEX_LIST

VERTEX_LIST(

{-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, {-5, 0, 5}

) /* VERTEX_LIST */

#endif /* VERTEX_LIST */

#ifdef NORMAL_LIST

NORMAL_LIST(

{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}

) /* NORMAL_LIST */

#endif /* NORMAL_LIST */

#ifdef UV_LIST

UV_LIST(

{0, 0, 0, 0}, {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}

) /* UV_LIST */

#endif /* UV_LIST */

#ifdef FACE_LIST

FACE_LIST(

0, 3, 2,
2, 1, 0

) /* FACE_LIST */

#endif /* FACE_LIST */
