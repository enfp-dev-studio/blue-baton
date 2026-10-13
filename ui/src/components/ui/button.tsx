import * as React from "react"
import { cva, type VariantProps } from "class-variance-authority"
import { cn } from "@/lib/utils"
import { Slot } from "radix-ui"

const buttonVariants = cva(
  "inline-flex shrink-0 items-center justify-center gap-2 text-sm font-bold tracking-wide uppercase whitespace-nowrap transition-all outline-none focus-visible:border-ring focus-visible:ring-[3px] focus-visible:ring-ring/50 disabled:pointer-events-none disabled:opacity-50 aria-invalid:border-destructive aria-invalid:ring-destructive/20 dark:aria-invalid:ring-destructive/40 [&_svg]:pointer-events-none [&_svg]:shrink-0 [&_svg:not([class*='size-'])]:size-4",
  {
    variants: {
      variant: {
        default: "border-[1.5px] border-foreground bg-primary text-primary-foreground shadow-hard-sm hover:bg-primary/85 active:translate-x-[2px] active:translate-y-[2px] active:shadow-none",
        destructive:
          "border-[1.5px] border-foreground bg-destructive text-white shadow-hard-sm hover:bg-destructive/90 focus-visible:ring-destructive/20 active:translate-x-[2px] active:translate-y-[2px] active:shadow-none",
        outline:
          "border-[1.5px] border-foreground bg-card shadow-hard-sm hover:bg-accent hover:text-accent-foreground active:translate-x-[2px] active:translate-y-[2px] active:shadow-none",
        secondary:
          "border-[1.5px] border-foreground bg-secondary text-secondary-foreground shadow-hard-sm hover:bg-secondary/80 active:translate-x-[2px] active:translate-y-[2px] active:shadow-none",
        ghost:
          "hover:bg-accent hover:text-accent-foreground",
        link: "text-foreground underline decoration-[1.5px] underline-offset-4 hover:bg-primary hover:text-primary-foreground",
      },
      size: {
        default: "h-9 px-4 py-2 has-[>svg]:px-3",
        xs: "h-6 gap-1 px-2.5 text-2xs has-[>svg]:px-1.5 [&_svg:not([class*='size-'])]:size-3",
        sm: "h-7 gap-1.5 px-3 text-xs has-[>svg]:px-2.5",
        lg: "h-10 px-6 has-[>svg]:px-4",
        icon: "size-9",
        "icon-xs": "size-6 [&_svg:not([class*='size-'])]:size-3",
        "icon-sm": "size-8",
        "icon-lg": "size-10",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
    },
  }
)

function Button({
  className,
  variant = "default",
  size = "default",
  asChild = false,
  ...props
}: React.ComponentProps<"button"> &
  VariantProps<typeof buttonVariants> & {
    asChild?: boolean
  }) {
  const Comp = asChild ? Slot.Root : "button"

  return (
    <Comp
      data-slot="button"
      data-variant={variant}
      data-size={size}
      className={cn(buttonVariants({ variant, size, className }))}
      {...props}
    />
  )
}

export { Button, buttonVariants }
