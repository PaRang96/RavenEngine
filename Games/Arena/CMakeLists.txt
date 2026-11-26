cmake_minimum_required(VERSION 3.28)
project(ArenaGame LANGUAGES CXX)
include("${RAVEN_ROOT}/cmake/RavenGame.cmake")
raven_add_game(Arena)