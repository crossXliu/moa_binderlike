# moa_binderlike
A simple driver to transmit messages cross different process.

.
├── README.md
├── binderlike
│   ├── Makefile
│   ├── binderlike-core.c
│   └── binderlike-core.h
└── usr-tst
    ├── Makefile
    └── main.c

binderlike directory is a kernel driver work in Linux 5.10.

usr-tst is a user space demo to control and test the driver.
