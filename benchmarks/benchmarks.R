library("tidyverse")
library("Rcpp")
library("RcppParallel")
library("RcppThread")
library("ggthemes")


Rcpp::sourceCpp(here::here("benchmarks/benchmarks.cpp"))


wait_for <- 5

plot_df <- function(df, title = NULL) {
  p <- df %>%
    pivot_longer(-n, "call") %>%
    ggplot(aes(n, value, color = call)) +
    geom_line(size = 0.6) +
    expand_limits(y = 0) +
    labs(color = "", linetype = "") +
    naglr::theme_naglr(plot_title_size = 12) +
    theme(legend.margin = margin(1, 1, 1, 1)) +
    theme(legend.position = "bottom") +
    scale_x_log10() +
    ylab("speedup") +
    labs(title = title)
  print(p)
}


ns <- 10^(2:6)
res <- benchEmpty(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "empty jobs")
ggsave(here::here("benchmarks/benchEmptyThread.pdf"), width = 7.5, height = 3)


ns <- 10^(2:6)
res <- benchSqrt(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "1000x sqrt")
ggsave(here::here("benchmarks/benchSqrt.pdf"), width = 7.5, height = 3)


ns <- 10^(2:6)
res <- benchSqrtWrite(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "1000x sqrt modify inplace")
ggsave(here::here("benchmarks/benchSqrtWrite.pdf"), width = 7.5, height = 3)

res <- benchSqrtImbalanced(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "n times sqrt (imbalanced)")
ggsave(here::here("benchmarks/benchSqrtImbalanced.pdf"), width = 7.5, height = 3)

ns <- 10^(2:6)
res <- benchSqrtWriteImbalanced(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "n times sqrt modify inplace (imbalanced)")
ggsave(here::here("benchmarks/benchSqrtWriteImbalanced.pdf"), width = 7.5, height = 3)

ns <- 10^(2:5)
res <- benchKDE(ns, 100, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "kernel density d = 10")
ggsave(here::here("benchmarks/benchKDE-10.pdf"), width = 7.5, height = 3)


ns <- 10^(2:5)
res <- benchKDE(ns, 100, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "kernel density d = 100")
ggsave(here::here("benchmarks/benchKDE-100.pdf"), width = 7.5, height = 3)

ns <- 10^(2:4)
res <- benchKendall(ns, 10, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "Kendall matrix (unbalanced) d = 10")
ggsave(here::here("benchmarks/benchKendall-10.pdf"), width = 7.5, height = 3)

ns <- 10^(2:4)
res <- benchKendall(ns, 100, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df, "Kendall matrix (unbalanced) d = 100")
ggsave(here::here("benchmarks/benchKendall-100.pdf"), width = 7.5, height = 3)
