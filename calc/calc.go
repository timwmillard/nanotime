package main

import (
	"fmt"
	"strconv"
)

func main() {
	n1 := int64(5)
	n2 := int64(9)
	num := n1 &^ n2
	fmt.Printf("num = %v\n", num)
	fmt.Printf("n1 = %v\n", strconv.FormatInt(n1, 2))
	fmt.Printf("n2 = %v\n", strconv.FormatInt(n2, 2))
	fmt.Printf("num = %v\n", strconv.FormatInt(num, 2))
}
