#!/bin/bash

# 1. 批量创建目录结构 -p 参数表示批量创建
mkdir -p models
mkdir -p include/{audio,core,model,utils,video}
mkdir -p src/{audio,core,model,utils,video}
mkdir -p examples

# 2. 创建 README.md
touch README.md

# 3. 关键: 给所有空目录添加 .gitkeep (这样Git才会追踪这些空文件夹)
find . -type d -not -path "./.git*" -exec touch {}/.gitkeep \;

# 4. 创建 .gitignore (防止提交垃圾文件)
cat << 'GITIGNORE_EOF' > .gitignore
# Prerequisites
*.d
*.test
*.install
*.o
*.obj
build/
out/
.vscode/
.DS_Store
*.lib
*.a
*.so
*.dll
GITIGNORE_EOF

