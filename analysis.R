# Script investigates analysis that is required to rigorously estimate the following parameters:
# - detect visits that belong to a series
# - remove outliers using a rigorous test
# - develop a model for prediction of the following
# - duration of a visit
# - start time of a visit
# - time windows for a visit

packages <- c('dplyr', 'tidyr', 'fpp2', 'car')
new_packages <- packages[!(packages %in% installed.packages()[,"Package"])]
if (length(new_packages)) {
    install.packages(new_packages)
}

library('dplyr')
library('tidyr')
library('fpp2')

data_set <- read.csv('/home/pmateusz/dev/cordia/output.csv')

# heat map of different visit start times
# visit_freq <- dataset %>% group_by(UserId,Tasks) %>% count()

# v <- spread(visit_freq, PlannedStartOrd, n, fill = 0)
# v_rest <- v[1:100]
# rownames(v_rest) <- v_rest$UserId
# heatmap(data.matrix(v_rest), scale = 'column', Rowv=NA, Colv=NA, col=paste("gray",99:1,sep=""))

# visit_groups <- dataset %>% group_by(UserId,Tasks,OriginalStartOrd) %>% count()

# ts_start <- ts(dataset %>% filter(UserId==22 & Tasks=='6-7' & CheckoutMethod <= 2 & RealDurationOrd > 0) %>% select(StartDate, RealDurationOrd))
# mod <- tslm(StartDate ~ RealDurationOrd, data = ts_start)

# autoplot(ts_start[,'StartDate'], series="Data")
# + autolayer(fitted(mod), series="Fitted")
# + guides(colour=guide_legend(title=" "))
# + xlab('Day')
# + ylab('Visit Start Time')

# mod <- ets(ts_start, damped=TRUE)
# mod %>% autoplot()
# autoplot(mod)


# visit_cout <- aggregate(dataset$VisitId, list(UserId=dataset$UserId), length)
# barplot(visit_cout$UserId, visit_cout$x)

# v <- aggregate(dataset$VisitId, list(UserId=dataset$UserId,OriginalStartTile=dataset$OriginalStart), length)
#