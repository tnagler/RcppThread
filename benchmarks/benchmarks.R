library("tidyverse")
library("Rcpp")
library("RcppParallel")
library("RcppThread")
library("ggthemes")


Rcpp::sourceCpp(here::here("benchmarks/benchmarks.cpp"))

ns <- exp(seq.int(log(50), log(10^5), length = 7))

wait_for <- 5

plot_df <- function(df) {
  p <- df %>%
    pivot_longer(-n, "call") %>%
    ggplot(aes(n, value, color = call)) +
    geom_line(size = 0.6) +
    expand_limits(y = 0) +
    labs(color = "", linetype = "") +
    ggthemes::theme_pander() +
    theme(legend.margin = margin(1, 1, 1, 1)) +
    theme(legend.position = "bottom")
  print(p)
}


res <- benchEmpty(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df)
ggsave("benchEmptyThread.pdf", width = 5.5, height = 3)


res <- benchSqrt(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df)
ggsave("benchSqrt.pdf", width = 5.5, height = 3)


res <- benchSqrtWrite(ns, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df)
ggsave("benchSqrtWrite.pdf", width = 5.5, height = 3)


res <- benchKDE(ns, 10, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df)
ggsave("benchKDE-10.pdf", width = 5.5, height = 3)


res <- benchKDE(ns, 100, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df)
ggsave("benchKDE-100.pdf", width = 5.5, height = 3)


res <- benchKendall(ns, 10, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df)
ggsave("benchKendall-10.pdf", width = 5.5, height = 3)


res <- benchKendall(ns, 100, wait_for)
df <- cbind(data.frame(n = ns), as.data.frame(res[, -1]))
plot_df(df)
ggsave("benchKendall-100.pdf", width = 5.5, height = 3)
