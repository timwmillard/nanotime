cat << EOF
#ifndef NANOTIME_H
#define NANOTIME_H

EOF

cat src/time.h

cat << EOF
#endif

EOF

cat << EOF

//////////////////////////////////////////////////////////////////////////////
//
//   IMPLEMENTATION
//

#ifdef NANOTIME_IMPLEMENTATION

EOF

cat src/time.c

cat << EOF
#endif
EOF

