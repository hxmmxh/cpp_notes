

# 定义
- 指针本事是一个对象，并且这个对象可以指向其他对象
- 所以const修饰的是指针本身还是指针指向的对象就是个问题
- 
- **顶层const**：指针本身是个常量
- **底层const**：指针所指向的对象是常量

# 说明

```cpp
int i = 0;
int *const p1 = &i;       //const修饰对象p1，p1不可改变，所以是顶层const
const int ci = 42;        //const修饰对象ci,ci不可改变,所以是顶层const
const int *p2 =&ci;       //const修饰指针，p2可以改变，ci不可改变，所以是底层const

// 执行对象拷贝时有限制，常量的底层const不能赋值给非常量的底层const(反过来可以)
int num_c = 3;
const int *p_c = &num_c;  //p_c为底层const的指针
//int *p_d = p_c;  //错误，不能将底层const指针赋值给非底层const指针
const int *p_d = p_c; //正确，可以将底层const指针复制给底层const指针

//const_cast只能改变运算对象的底层const
int num_e = 4;
const int *p_e = &num_e;
//*p_e = 5;  //错误，不能改变底层const指针指向的内容
int *p_f = const_cast<int *>(p_e);  //正确，const_cast可以改变运算对象的底层const。但是使用时一定要知道num_e不是const的类型。
*p_f = 5;  //正确，非顶层const指针可以改变指向的内容
cout << num_e;  //输出5

```