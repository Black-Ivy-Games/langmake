# Mosslight Assembly
An original strategy reference with 128 runtime-spawned, articulated workers,
navigation around a wall, staggered group orders, persistent cargo and UI.

Run `langmake play --project <this-directory>` or repository command
`build/langmake.exe play --project examples/strategy`. WASD moves the commander. C orders
gathering on the amber field; wait for arrivals, then V brings cargo to the blue
home field. N creates a worker (160 cap); H dismisses the last worker. F6 saves
slot `assembly`; F9 loads after a save. Deliver 100 lights to complete the task.
Keys 1/2/3 select all/first half/second half. C/V affect the selection; B follows
the commander continuously, J recalls once. Formation columns/spacing are editable
through the orders module. Current selection and orders survive saves.

The example stresses engine capacity and is deliberately a small creative slice.
Units have simple task states and capsule-free navigation motion; they do not
represent a complete RTS unit/combat framework. Procedural worker poses, cargo,
navigation status, group queues and scores are inspectable. CPU skinning has a
visible-vertex budget; keep mass-unit art simple and inspect timings.

Human feedback: Can you follow a group order visually? Is the obstacle route
clear? Which command or worker behavior would make the next iteration useful?
